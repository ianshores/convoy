#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Profile actual transform and complete uniform-block paths in saved candidates."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import random
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',required=True)
p.add_argument('--labels',nargs='+',required=True)
a=p.parse_args()
logs=ROOT/a.root
commands=[]
for label in a.labels:
    meta=json.loads((logs/label/'metadata.json').read_text())
    build=ROOT/'build/fft-optimization'/label
    packed='forward_half_packed' in (build/'include/heavyverb_fft.hpp').read_text()
    for name,source in [('active-components','fft_active_benchmark.cpp'),('partition-components','partition_benchmark.cpp')]:
        flags=meta['flags']+(['-DHEAVYVERB_PROFILE_PACKED'] if packed else [])
        command=['c++',*flags,str(ROOT/'tests'/source),*meta['link_objects'],'-o',str(build/name)]
        subprocess.run(command,check=True)
        commands.append({'label':label,'command':command,'source_sha256':hashlib.sha256((ROOT/'tests'/source).read_bytes()).hexdigest()})
for trial in range(1,6):
    order=a.labels.copy();random.Random(941+trial).shuffle(order)
    for label in order:
        for name in ['active-components','partition-components']:
            destination=logs/label/f'{name}-{trial}.csv'
            if destination.exists(): continue
            r=subprocess.run(['taskset','-c','1',str(ROOT/'build/fft-optimization'/label/name)],text=True,capture_output=True,check=True)
            destination.write_text(r.stdout)
    print('component trial',trial,flush=True)
summary={}
for label in a.labels:
    summary[label]={}
    for name in ['active-components','partition-components']:
        series={}
        for file in (logs/label).glob(name+'-*.csv'):
            for row in csv.DictReader(file.open()):
                for key,value in row.items():
                    if key!='fft_size':series.setdefault(row['fft_size']+':'+key,[]).append(float(value))
        summary[label][name]={key:statistics.median(values) for key,values in series.items()}
(logs/'component-metadata.json').write_text(json.dumps({'runs':5,'seed':'941 + trial','logical_cpu':1,'commands':commands},indent=2)+'\n')
(logs/'components-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
