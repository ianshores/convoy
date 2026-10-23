#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Confirm saved FFT candidates sequentially, preserving source and raw trials."""
import argparse
import csv
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', required=True)
parser.add_argument('--labels', nargs='+', required=True)
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--seconds', type=int, default=30)
parser.add_argument('--prefix', default='confirmation')
args = parser.parse_args()
logs = ROOT / args.root
schedule = []
for trial in range(1, args.runs + 1):
    order = args.labels.copy()
    random.Random(572 + trial).shuffle(order)
    schedule.append(order)
    for label in order:
        target = logs / label / f'{args.prefix}-{trial}.csv'
        if target.exists():
            continue
        binary = ROOT / 'build/fft-optimization' / label / 'benchmark'
        result = subprocess.run(['taskset', '-c', '1', str(binary)],
            env=dict(os.environ, HEAVYVERB_BENCH_SECONDS=str(args.seconds)),
            capture_output=True, text=True, check=True)
        target.write_text(result.stdout)
        print(label, args.prefix, trial, flush=True)
(logs / f'{args.prefix}-schedule.json').write_text(json.dumps({
    'runs': args.runs, 'audio_seconds': args.seconds, 'schedule': schedule,
}, indent=2) + '\n')
summary = {}
for folder in sorted(logs.iterdir()):
    if not folder.is_dir() or not (folder / 'metadata.json').exists():
        continue
    series = {}
    for prefix in {'run', args.prefix}:
        rows = {}
        for file in sorted(folder.glob(prefix + '-*.csv')):
            for row in csv.DictReader(file.open()):
                key = ','.join(row[k] for k in ['ir_seconds', 'channels', 'callback'])
                rows.setdefault(key, []).append(row)
        if not rows:
            continue
        series[prefix] = {key: {
            'runs': len(values),
            'cpu_median': statistics.median(float(v['cpu_percent']) for v in values),
            'cpu_min': min(float(v['cpu_percent']) for v in values),
            'cpu_max': max(float(v['cpu_percent']) for v in values),
            'p99_median_us': statistics.median(float(v['p99_us']) for v in values),
            'worst_callback_us': max(float(v['max_us']) for v in values),
            'deadline_us': float(values[0]['deadline_us']),
        } for key, values in rows.items()}
    summary[folder.name] = series
(logs / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
with tarfile.open(logs / 'source-snapshots.tar.gz', 'w:gz') as archive:
    for folder in sorted(logs.glob('r[0-9]*-*')):
        if (folder / 'metadata.json').exists():
            archive.add(ROOT / 'build/fft-optimization' / folder.name / 'include',
                        arcname=folder.name + '/include')
            sources = ROOT / 'build/fft-optimization' / folder.name / 'tests'
            if sources.exists():
                archive.add(sources, arcname=folder.name + '/tests')
for label in args.labels:
    row = summary[label][args.prefix]
    print(label, {key: round(row[key]['cpu_median'], 5)
                 for key in ['1,1,256', '1,2,256', '4,1,256', '4,2,256']})
