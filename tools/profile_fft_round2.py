#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Measure FFT pairs and complete uniform partition blocks from saved builds."""
import json, random, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
logs=root/'docs/performance/fft-round2'
labels=['r2-00-control','r2-05-paired-mac','r2-00-pffft']
for label in labels:
    meta=json.loads((logs/label/'metadata.json').read_text())
    target=root/'build/fft-optimization'/label/'partition-components'
    subprocess.run(['c++',*meta['flags'],str(root/'tests/partition_benchmark.cpp'),*meta['link_objects'],'-o',str(target)],check=True)
for trial in range(1,6):
    order=labels.copy();random.Random(916+trial).shuffle(order)
    for label in order:
        for name in ['components','partition-components']:
            destination=logs/label/f'{name}-{trial}.txt'
            if destination.exists(): continue
            result=subprocess.run(['taskset','-c','1',str(root/'build/fft-optimization'/label/name)],capture_output=True,text=True,check=True)
            destination.write_text(result.stdout)
    print('component trial',trial,flush=True)
