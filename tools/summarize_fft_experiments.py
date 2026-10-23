#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize all raw runs without deleting outliers or mixing trial series."""
import csv
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1] / "docs/performance/fft-optimization"
summary = {}
for folder in sorted(ROOT.iterdir()):
    if not folder.is_dir():
        continue
    files = sorted(folder.glob("confirmation-*.csv")) or sorted(folder.glob("run-*.csv"))
    if not files:
        continue
    grouped = {}
    for source in files:
        for row in csv.DictReader(source.open()):
            key = ",".join(row[name] for name in ("ir_seconds", "channels", "callback"))
            grouped.setdefault(key, []).append(row)
    summary[folder.name] = {
        key: {
            "trials": len(rows),
            "cpu_median": statistics.median(float(row["cpu_percent"]) for row in rows),
            "cpu_min": min(float(row["cpu_percent"]) for row in rows),
            "cpu_max": max(float(row["cpu_percent"]) for row in rows),
            "p99_median_us": statistics.median(float(row["p99_us"]) for row in rows),
            "max_wall_us": max(float(row["max_us"]) for row in rows),
        }
        for key, rows in grouped.items()
    }
(ROOT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
for label, cases in summary.items():
    mono, two = cases["4,1,256"], cases["4,2,256"]
    print(f"{label}: mono {mono['cpu_median']:.4f}%, two {two['cpu_median']:.4f}%")
