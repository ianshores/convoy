# Third-round records

See [full report](../../../FFT_ROUND3_REPORT.md) and
[recommendations](../../../FFT_ROUND3_RECOMMENDATIONS.md).

`summary.json` distinguishes initial `run` trials from seven shuffled
`confirmation` trials; the report uses confirmation medians. Each label stores
raw CSVs, metadata/commands/source hashes and correctness output. Failed r3-02
is retained. `confirmation-schedule.json` records actual order.
`source-snapshots.tar.gz` contains exact headers and compiled test sources;
`snapshot-audit.txt` verifies them. `final-source.json` records formatted shipping
headers, equivalent to the measured final candidate. `components-summary.json`
uses five shuffled trials of the active transform and full partition block.
Legacy `components.txt` MAC numbers refer to the old split-plane helper, not the
production packed mixer.

`validation.txt`, `validation-commands.json`, `quality-*.csv`, `plugins.json`,
and `runtime/` hold numerical, integration, sanitizer, allocation, callback and
stress evidence. Plugin binary hashes describe local builds, not uploaded binaries.

Use the verification commit linked in the final repository history to reproduce
PFFFT comparisons. Later commits remove that dependency. From that commit:

```sh
python3 tools/fft_experiment.py --help
python3 tools/fft_round.py --help
python3 tools/profile_fft_round.py --help
make test quality
```

Restore a candidate's headers from the archive into an isolated checkout, build
with its metadata flags, and follow recorded commands. Benchmark sequentially on
logical CPU 1, with ordinary priority; keep raw outliers. Do not run timing trials
concurrently with builds or other benchmarks. `cutoff/` is a separate optional
quality/performance experiment and does not contribute to the full-quality win.
