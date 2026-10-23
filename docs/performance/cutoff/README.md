# Cutoff experiment and live control

Follow-up to verified optimization commit `69f7962`. Machine: Intel Core Ultra 9
185H, Linux, GCC 16.2.1, logical CPU 1, `-O3 -std=c++17`, no fast-math or FMA,
ordinary priority. All IRs are synthetic, four seconds at 48kHz, mono, with
256/2048 partitions and 256-sample callbacks/latency unless a log says otherwise.

`final/` contains seven confirmation trials, exact source/executable hashes,
commands, order, and aggregated results. Each trial processes 30 seconds of
steady audio after full-history warmup. The outer order is shuffled; cutoff
policy order rotates per run. CPU measurement encloses the complete processing
loop (the legacy full benchmark also instruments each callback). Compare
policies within a benchmark, not the absolute timing across different harnesses.

`trial-*.csv` are retained preliminary live-control runs. Their
`live_partition_cutoff` rows incorrectly report zero `tail_l1_bound`; that
reporting bug is fixed in `final/`. CPU and residual measurements are unaffected.
Earlier offline trials are in `../fft-round3/cutoff/`.

The reference signal for residual measurements is 8192 samples of uniform
wideband noise at +/-0.25, followed by silence through the complete tail.
IR coefficients are uniform noise at +/-0.1 under an exponential envelope
with 52.115, 120, or 160dB decay across four seconds. A -96dB coefficient
threshold is approximately 0.000015849. It is absolute IR gain, not a guaranteed
-96dBFS output error. Tail length is rounded up to whole partitions in the live
control; the IR is never rewritten and all input history remains available.

Reproduce current-engine trials in a new output directory:

```sh
python3 tools/benchmark_cutoff.py --output build/cutoff-results --runs 7
```

Optional `--reference label=/absolute/path/to/benchmark` compares saved
throughput controls. The verified custom and PFFFT AVX binaries used here were
built from the exact sources archived in round three; executable hashes are
recorded. PFFFT reproduction requires checkout of commit `69f7962`.

`validation.txt` and `validation-commands.json` record core/FFT/quality, export,
native Pd, CMake, sanitizer, SSE/scalar and uniform-path checks. The separate
`live-allocation.txt` check also intercepts C++ new during cutoff changes.
`clap-control.txt` checks the built CLAP's metadata, automation flag, stereo
render, live cutoff, and full restoration without REAPER. The demo's strong
late reflections make -96dB preserve its full length; the test uses -1dB to
exercise a deliberate, clearly audible trim. VST3 is built from the same DSP
and parameter definition but has not received a separate host render test.

Issues resolved: the first Pd demo connection referenced a text object rather
than its parameter receiver; the index is corrected. The CLAP probe initially
expected an underscore in the displayed name, while DPF uses spaces. Its first
-96dB assertion also incorrectly assumed the demo had no strong late reflections;
the probe now uses -1dB, while the engine correctly preserves those reflections.

Headless plugin check (DPF revision is pinned in `tools/build_plugins.py`):

```sh
c++ -O2 -std=c++17 -I/path/to/DPF/distrho/src tests/clap_control.cpp -ldl -o build/clap_control
build/clap_control /absolute/path/to/HeavyVerbDemo.clap
```

The `timing.csv` probe was built with `c++ -O3 -std=c++17 -Iinclude
tests/cutoff_timing.cpp -o build/cutoff_timing` and run using `taskset -c 1`.
It includes the setter cost while automating. No benchmark ran concurrently
with builds. `pd-control.txt` uses `tests/pd.py /path/to/pd --cutoff`.
