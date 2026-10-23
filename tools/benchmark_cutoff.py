#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Sequential cutoff trials plus optional saved reference throughput controls."""
import argparse,csv,hashlib,json,os,platform,random,statistics,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--runs',type=int,default=7)
p.add_argument('--cpu',type=int,default=1)
p.add_argument('--reference',action='append',default=[],help='Label=path to saved benchmark executable')
a=p.parse_args(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=False)
build=ROOT/'build/cutoff-final';build.mkdir(exist_ok=True)
meta={'cpu':next(x.split(':',1)[1].strip() for x in Path('/proc/cpuinfo').read_text().splitlines() if x.startswith('model name')),'compiler':subprocess.check_output(['c++','--version'],text=True).splitlines()[0],'platform':platform.platform(),'logical_cpu':a.cpu,'runs':a.runs,'audio_seconds':30,'priority':'ordinary','commands':[],'sha256':{}}
for name,source in [('cutoff','tests/cutoff_benchmark.cpp'),('full','tests/benchmark.cpp')]:
 cmd=['c++','-O3','-std=c++17','-I'+str(ROOT/'include'),str(ROOT/source),'-o',str(build/name)]
 subprocess.run(cmd,check=True);meta['commands'].append(cmd)
refs={'full':build/'full'}
for entry in a.reference:
 label,path=entry.split('=',1);refs[label]=Path(path).resolve()
for path in [*refs.values(),build/'cutoff',*(ROOT/'include').glob('*.hpp'),ROOT/'tests/cutoff_benchmark.cpp',ROOT/'tests/benchmark.cpp']:
 meta['sha256'][str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
rng=random.Random(1023);schedule=[]
for run in range(a.runs):
 tasks=['cutoff',*refs];rng.shuffle(tasks)
 for label in tasks:
  exe=build/'cutoff' if label=='cutoff' else refs[label]
  cmd=['taskset','-c',str(a.cpu),str(exe),str(run)]
  result=subprocess.run(cmd,check=True,capture_output=True,text=True,env={**os.environ,'HEAVYVERB_BENCH_SECONDS':'30'})
  (out/f'{label}-{run}.csv').write_text(result.stdout);schedule.append({'run':run,'label':label,'command':cmd})
  print(label,run,flush=True)
meta['schedule']=schedule;(out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
summary={}
for label in ['cutoff',*refs]:
 groups={}
 for file in out.glob(label+'-*.csv'):
  for row in csv.DictReader(file.open()):
   key=','.join(row[k] for k in (('decay_db','policy') if label=='cutoff' else ('ir_seconds','channels','callback')))
   groups.setdefault(key,[]).append(row)
 summary[label]={key:{'cpu_median':statistics.median(float(r['cpu_percent']) for r in rows),'cpu_min':min(float(r['cpu_percent']) for r in rows),'cpu_max':max(float(r['cpu_percent']) for r in rows),'runs':len(rows),'sample':rows[0]} for key,rows in groups.items()}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
