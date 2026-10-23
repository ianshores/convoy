#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize raw round-two trials without dropping outliers."""
import csv, json, statistics
from pathlib import Path
root = Path(__file__).resolve().parents[1] / 'docs/performance/fft-round2'
result = {}
for folder in sorted(root.iterdir()):
    if not folder.is_dir(): continue
    series = {}
    for prefix in ('run', 'confirmation'):
        rows = {}
        for file in sorted(folder.glob(prefix+'-*.csv')):
            for row in csv.DictReader(file.open()):
                key = ','.join(row[x] for x in ('ir_seconds','channels','callback'))
                rows.setdefault(key,[]).append(row)
        if not rows: continue
        series[prefix] = {key: {
            'runs': len(values),
            'cpu_median': statistics.median(float(v['cpu_percent']) for v in values),
            'cpu_min': min(float(v['cpu_percent']) for v in values),
            'cpu_max': max(float(v['cpu_percent']) for v in values),
            'p99_median_us': statistics.median(float(v['p99_us']) for v in values),
            'worst_callback_us': max(float(v['max_us']) for v in values),
            'deadline_us': float(values[0]['deadline_us']),
        } for key,values in rows.items()}
    if series:
        result[folder.name] = series
        for name,values in series.items():
            print(folder.name,name, 'mono',round(values['4,1,256']['cpu_median'],5),'two',round(values['4,2,256']['cpu_median'],5))
(root/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
