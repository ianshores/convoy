#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Final matched comparison of original custom, production custom and PFFFT."""
import json
import os
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CONFIGS = (("final-original", ["--baseline"]), ("final-custom", ["--production"]), ("final-pffft", ["--pffft"]))
for label, options in CONFIGS:
    output = ROOT / "docs/performance/fft-optimization" / label
    if not output.exists():
        subprocess.run([sys.executable, str(ROOT / "tools/fft_experiment.py"), label, "--runs", "0", *options], check=True)
environment = dict(os.environ, HEAVYVERB_BENCH_SECONDS="30")
schedule = []
for trial in range(1, 6):
    order = [label for label, _ in CONFIGS]
    random.Random(171 + trial).shuffle(order)
    schedule.append(order)
    for label in order:
        destination = ROOT / "docs/performance/fft-optimization" / label / f"confirmation-{trial}.csv"
        if destination.exists():
            continue
        executable = ROOT / "build/fft-optimization" / label / "benchmark"
        result = subprocess.run(["taskset", "-c", "1", str(executable)], env=environment, capture_output=True, text=True, check=True)
        destination.write_text(result.stdout)
        print(label, "trial", trial, flush=True)
(ROOT / "docs/performance/fft-optimization/final-schedule.json").write_text(json.dumps({"runs": 5, "measured_audio_seconds": 30, "schedule": schedule}, indent=2) + "\n")
subprocess.run([sys.executable, str(ROOT / "tools/summarize_fft_experiments.py")], check=True)
