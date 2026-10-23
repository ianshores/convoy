#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Headless checks for the built WAV-selector plugin and its generated graph."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--dpf', type=Path, required=True)
parser.add_argument('--sanitize', action='store_true', help='Instrument loader, runtime and message queue with ASan/UBSan')
args = parser.parse_args()
out = args.output.resolve()
fixtures = out / 'test-fixtures'
fixtures.mkdir(exist_ok=True)
checks = out / ('checks-sanitized' if args.sanitize else 'checks')
checks.mkdir(exist_ok=True)


def run(command):
    subprocess.run(list(map(str, command)), cwd=ROOT, check=True)


for name in ['clap_control', 'clap_ir']:
    exe = checks / name
    run(['c++', '-O2', '-std=c++17', '-I' + str(args.dpf.resolve() / 'distrho/src'),
         ROOT / 'tests' / f'{name}.cpp', '-lsndfile', '-ldl', '-pthread', '-o', exe])
    run([exe, out / 'bin/HeavyVerbDemo.clap', *([fixtures] if name == 'clap_ir' else [])])

flags = ['-O1' if args.sanitize else '-O2', '-DHV_SIMD_NONE=1', '-Iinclude', '-Iplugin', '-I' + str(out / 'c')]
if args.sanitize:
    flags += ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    objects = []
    for source in sorted((out / 'c').iterdir()):
        if source.suffix not in ('.c', '.cpp'):
            continue
        obj = checks / (source.name + '.o')
        cpp = source.suffix == '.cpp'
        run(['c++' if cpp else 'cc', *flags, *(['-std=c++17'] if cpp else []), '-c', source, '-o', obj])
        objects.append(obj)
else:
    objects = [out / 'build/HeavyContext.cpp.o', out / 'build/Heavy_HeavyVerbDemo.cpp.o',
               *sorted((out / 'build').glob('Hv*.o'))]

run(['c++', *flags, '-std=c++17', 'tests/wav_loader.cpp', '-lsndfile', '-lsamplerate', '-o', checks / 'wav_loader'])
run([checks / 'wav_loader', fixtures])
run(['c++', *flags, '-std=c++17', 'tests/ir_runtime.cpp', *objects, '-lsndfile', '-lsamplerate', '-pthread',
     '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc', '-o', checks / 'ir_runtime'])
run([checks / 'ir_runtime', fixtures])
pipe = checks / 'HvLightPipe.c.o' if args.sanitize else out / 'build/HvLightPipe.c.o'
utils = checks / 'HvUtils.c.o' if args.sanitize else out / 'build/HvUtils.c.o'
run(['c++', *flags, '-std=c++17', 'tests/lightpipe.cpp', pipe, utils, '-o', checks / 'lightpipe'])
run([checks / 'lightpipe'])
