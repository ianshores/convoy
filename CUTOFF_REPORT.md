# IR tail cutoff: measurements and plugin control

2026-10-22. Zeroing individual quiet IR samples does not reduce the amount of
FFT or partition-mixing work. Skipping whole trailing partitions does. The
optional **IR Tail Cutoff dB** plugin parameter does this without modifying the
stored IR, changing latency, or clearing input history. **0 means off**, and
is the default. Negative settings select an absolute IR-coefficient threshold,
clamped at -144dB. For example, -96dB means a coefficient magnitude of about
0.000015849. This is not a promise of inaudible error or a -96dBFS output floor.

## Measurement conditions

Intel Core Ultra 9 185H laptop, Linux, GCC 16.2.1, logical CPU 1, ordinary priority,
`-O3 -std=c++17`, no fast-math or FMA. Runtime custom AVX path. Mono, 48kHz,
four-second synthetic IR, 256-sample callbacks/latency, 256/2048 partitions.
Seven repeated 30-second-audio trials after warming the full history. CPU is
elapsed processing as a percentage of one core's audio-time budget. Raw data,
commands, exact hashes, and order are in [the log directory](docs/performance/cutoff/README.md).

Each IR is noise under an exponential envelope, starting at +/-0.1. The decay
column is the envelope's drop over four seconds, not a measured acoustic RT60.
Output residuals use 8192 samples of noise at +/-0.25 followed by silence through
the complete tail; they are signal-specific comparisons against the full IR.

## Live control at -96dB

| Envelope decay | Full CPU | Cutoff CPU | CPU reduction | Retained IR | Peak output difference |
|---|---:|---:|---:|---:|---:|
| 52.1dB | 0.1489% | 0.1503% | -0.9% | 4.000s | Exactly zero |
| 120.0dB | 0.1480% | 0.1045% | 29.4% | 2.560s | -70.5dBFS |
| 160.0dB | 0.1479% | 0.0851% | 42.4% | 1.920s | -71.0dBFS |

The slow-decay IR cannot lose a whole partition; its roughly 1% timing difference
is noise/overhead, not useful savings. The faster decays save approximately 29%
and 42%. These are relative reductions from a small starting cost, not 29 or 42
percentage points of a core. The current demo IR also contains strong late
reflections, which correctly prevent shortening it at -96dB.

## Other policies tested

| Policy at -96dB | 52dB decay CPU | 120dB decay CPU | 160dB decay CPU | Interpretation |
|---|---:|---:|---:|---|
| floor_coefficients | 0.1487% | 0.1469% | 0.1475% | Zero individual coefficients; no structural savings |
| trim_peak_tail | 0.1487% | 0.1025% | 0.0865% | Offline sample-exact trim; requires rebuilding engine |
| trim_l1_tail | 0.1484% | 0.1479% | 0.1379% | Conservative bound on the sum of removed magnitudes |

Individual coefficient flooring changes the test output by peaks of roughly
-68 to -78dBFS while retaining all FFT work. Many small coefficients contribute
to the same output sample, so their effects add. The live tail policy has peaks
near -70 to -71dBFS in the two shortened examples. Listen before choosing a cutoff.
Neither result establishes whether that difference is audible in a particular mix.

The conservative policy removes a suffix only while the sum of its absolute
coefficients stays below 10^(-96/20). For input bounded by magnitude 1, the
triangle inequality bounds the ideal-arithmetic output error by that same
amplitude, before downstream gain. It trims little in these tests: no meaningful
CPU saving for the 120dB decay, about 7% for the 160dB decay. Float32 roundoff adds
its own much smaller error. This policy is an offline comparison, not an extra
plugin mode. Input/output sample flooring and silence-sleep detection are not
implemented; flooring output after processing would not recover spent CPU.

## How the control works

The IR's peak suffix envelope is computed once during setup. Control changes
look up the last partition containing a coefficient at or above the threshold.
Only trailing whole partitions are skipped. A quiet gap never hides a later
reflection. At least one partition is retained; two-stage processing always
preserves the small-block early stage as well. The threshold is relative to IR
coefficient unity, not the IR peak or the input signal level. Gain/normalization
of the IR therefore affects the setting's meaning.

Gains fade over eight processing-block updates; at the usual 2048-sample tail
size and 48kHz this is about 341ms, plus buffered overlap/latency. Shorter
partitions fade faster. This is a blockwise transition, not a guarantee of
artifact-free modulation at every setting. Memory and full input history remain
allocated, allowing the complete tail to return without reload or reconstruction.
Call the C++ setter on the audio thread, serialized with process/reset. The Heavy
adapter delivers queued parameter messages there. Native Pd reads its second
float inlet in the audio callback.

A separate 30-second, two-channel automation probe on the CPU above alternates
between off and -96dB once a second. It averages about 0.51% CPU versus 0.30%
with cutoff off, because transition mixing is more expensive than stable batched
mixing. Maximum measured callback times stay below deadlines in that run, with
zero misses at 64/256-sample callbacks. This is not a hard realtime guarantee.
Rapid continuous automation may eliminate the performance benefit; stable
settings are the intended use.

## Default-engine performance after adding the control

The control is off for this comparison. Seven shuffled repeats of the standard
eight-workload benchmark use the same Core Ultra 9 185H, compiler, sample rate,
partition and latency conditions as [round three](FFT_ROUND3_REPORT.md). Saved
verified-custom and AVX-targeted PFFFT binaries are hashed in the records.

| IR seconds | Channels | Callback | Current custom CPU | Verified custom CPU | PFFFT AVX CPU | Saving vs PFFFT AVX |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 64 | 0.073265% | 0.073383% | 0.076228% | 3.89% |
| 1 | 1 | 256 | 0.071294% | 0.069366% | 0.074081% | 3.76% |
| 1 | 2 | 64 | 0.145760% | 0.143363% | 0.151062% | 3.51% |
| 1 | 2 | 256 | 0.140645% | 0.141693% | 0.150193% | 6.36% |
| 4 | 1 | 64 | 0.148046% | 0.149374% | 0.172389% | 14.12% |
| 4 | 1 | 256 | 0.149383% | 0.154972% | 0.174398% | 14.34% |
| 4 | 2 | 64 | 0.315655% | 0.320983% | 0.361370% | 12.65% |
| 4 | 2 | 256 | 0.321755% | 0.313473% | 0.360755% | 10.81% |

The current custom engine remains faster in all eight workloads here, by 3.5–14.3%
relative to the AVX-targeted control. The cutoff-off change versus the verified
custom binary ranges roughly -4% to +3%, without a consistent regression.
These are full-convolver measurements, not a claim that the isolated FFT is faster.

## Validation and delivery

- Core/FFT checks, independent full-tail double-precision reference, and seven
  CMake tests pass. Full-quality results remain at the same float32 error floor.
- 81 settled cutoff/restoration cases and an independent direct overlap-add model
  of live/rapid parameter transitions pass; worst error 3.27e-8. The same cutoff
  suite passes scalar, forced SSE, uniform scheduling, and ASan/UBSan builds.
- C and C++ allocation interception finds no allocations while moving the control,
  processing, or resetting.
- Fifteen exported baseline stereo renders and nine cutoff/restoration stereo
  renders through the actual Heavy parameter receiver pass at three sample rates.
- Native Pd full-response and cutoff-inlet renders pass.
- The built CLAP passes a headless host test of parameter bounds/default, automation,
  stereo impulse rendering, trimming and restoration (restored output matches
  exactly). Its deliberate -1dB test setting removes the demo's strong reflections;
  it is a functional test, not a recommended listening setting.
- Linux CLAP and VST3 builds are ready in `build/effect-cutoff/bin`. VST3 uses the
  same DSP and parameter generator but has no separate host render test here.
  Wwise source includes the parameter; no Wwise/Unreal SDK build was attempted.

Verified optimizations and full PFFFT comparisons were pushed in commit `69f7962`
before dependency removal. The current source uses only the custom MIT FFT.
PFFFT sources/linkage are removed from the core, Pd, export, and plugin builds;
historical raw data and source snapshots remain available. Reproducing PFFFT
requires the verification commit. No Cradle session or interactive DAW was used.
