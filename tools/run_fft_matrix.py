#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reproduce the guide experiments, then alternate longer confirmation runs."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FMA_FLAGS = ["-mfma", "-mavx", "-mprefer-vector-width=128", "-ffp-contract=off"]
CONFIGS = [("00-original", ["--baseline"])]
CONFIGS += [(f"{step:02d}-{name}", ["--define", f"HEAVYVERB_FFT_STEP={step}"]) for step, name in enumerate(
    ["real", "layout", "twiddles", "simd", "radix4", "unordered", "prescale", "first-product"], start=1)]
CONFIGS += [
    ("09-explicit-mac", ["--define", "HEAVYVERB_EXPLICIT_MAC"]),
    ("10-fma-control", ["--cxx-flag=" + flag for flag in FMA_FLAGS]),
    ("11-fma", ["--define", "HEAVYVERB_FFT_FMA", *["--cxx-flag=" + flag for flag in FMA_FLAGS]]),
    ("12-chunk", ["--define", "HEAVYVERB_CHUNK_WRAPPER"]),
    ("13-tail16", ["--define", "HEAVYVERB_CHUNK_WRAPPER", "--define", "HEAVYVERB_TAIL_FACTOR=16"]),
    ("14-tail4", ["--define", "HEAVYVERB_CHUNK_WRAPPER", "--define", "HEAVYVERB_TAIL_FACTOR=4"]),
    ("15-bin-major", ["--define", "HEAVYVERB_BIN_MAJOR"]),
    ("16-compact-plan", ["--define", "HEAVYVERB_COMPACT_PLAN"]),
    ("17-scalar", ["--define", "HEAVYVERB_FFT_SCALAR"]),
    ("18-custom-uniform", ["--define", "HEAVYVERB_UNIFORM_PARTITIONS"]),
    ("19-pffft", ["--pffft"]),
    ("20-pffft-uniform", ["--pffft", "--define", "HEAVYVERB_UNIFORM_PARTITIONS"]),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()
    if args.prepare:
        for label, options in CONFIGS:
            label = "confirm-" + label
            output = ROOT / "docs/performance/fft-optimization" / label
            if output.exists():
                required = [output / "correctness.txt", ROOT / "build/fft-optimization" / label / "benchmark"]
                if not all(path.exists() for path in required):
                    raise RuntimeError(f"Incomplete preparation: {label}; preserve its log and use a fresh label before retrying")
                continue
            subprocess.run([sys.executable, str(ROOT / "tools/fft_experiment.py"), label, "--runs", "0", *options], check=True)
    environment = dict(os.environ, HEAVYVERB_BENCH_SECONDS=str(args.seconds))
    schedule = []
    for trial in range(1, args.runs + 1):
        order = ["confirm-" + label for label, _ in CONFIGS]
        random.Random(171 + trial).shuffle(order)
        schedule.append(order)
        for label in order:
            output = ROOT / "docs/performance/fft-optimization" / label / f"confirmation-{trial}.csv"
            if output.exists():
                continue
            executable = ROOT / "build/fft-optimization" / label / "benchmark"
            result = subprocess.run(["taskset", "-c", "1", str(executable)], env=environment, capture_output=True, text=True, check=True)
            output.write_text(result.stdout)
            print(label, "confirmation", trial, flush=True)
    metadata = {"runs": args.runs, "measured_audio_seconds": args.seconds, "schedule": schedule,
                "method": "Sequential runs in reproducibly shuffled order; identical benchmark source, compiler flags except explicit experiment, CPU affinity 1. All raw results retained."}
    (ROOT / "docs/performance/fft-optimization/confirmation-schedule.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
