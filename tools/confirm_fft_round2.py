#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Interleave immutable second-round experiment binaries and retain every trial."""
import argparse, json, os, random, subprocess, tarfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--labels', nargs='+')
parser.add_argument('--prefix', default='confirmation')
parser.add_argument('--runs',type=int,default=5)
args=parser.parse_args()
logs=root/'docs/performance/fft-round2'
labels=args.labels or sorted(p.name for p in logs.iterdir() if p.is_dir() and (p/'metadata.json').exists() and p.name.startswith('r2-'))
schedule=[]
for trial in range(1,args.runs+1):
    order=labels.copy();random.Random(280+trial).shuffle(order);schedule.append(order)
    for label in order:
        destination=logs/label/f'{args.prefix}-{trial}.csv'
        if destination.exists(): continue
        executable=root/'build/fft-optimization'/label/'benchmark'
        result=subprocess.run(['taskset','-c','1',str(executable)],env=dict(os.environ,HEAVYVERB_BENCH_SECONDS='30'),text=True,capture_output=True,check=True)
        destination.write_text(result.stdout)
        print(label, args.prefix, trial, flush=True)
(logs/f'{args.prefix}-schedule.json').write_text(json.dumps({'runs':args.runs,'audio_seconds':30,'schedule':schedule},indent=2)+'\n')
# Preserve code as well as hashes: experiments may be rejected from production.
with tarfile.open(logs/'source-snapshots.tar.gz','w:gz') as archive:
    for folder in sorted(logs.glob('r2-*')):
        if (folder/'metadata.json').exists():
            archive.add(root/'build/fft-optimization'/folder.name/'include',arcname=folder.name+'/include')
subprocess.run(['python3',str(root/'tools/summarize_fft_round2.py')],check=True)
