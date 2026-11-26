# Performance measurement harness

Hydra ships four measurement commands. No speedup is claimed from a single run.
`bench-tui` and `perf-attention` can fail; `bench-i1` fails only on harness errors
(see below); `bench-core` reports timings and parity for a person to read.

```sh
make bench-core     # shell and native snapshot timings, with row-count and byte parity
make bench-tui      # native store read, render and startup/CPU at 5, 20 and 100 heads
make bench-i1       # real worktrees and a seeded worker workload (below)
make perf-attention PERF_ATTENTION_ARGS="--out DIR"   # annotated attention corpus
```

`make bench-tui` prints every head count and then fails if any of them exceeded a
budget: native store read over 1,000 ms, render over 100 ms, interactive startup
over 3,000 ms or interactive CPU over 40% of the window (`scripts/bench-tui.sh`).

The study of October 2026 (a 1/10/50-head matrix with published evidence under
`docs/evidence/performance/`) measured the shell data reader that 5.0 removed and
has been retired. To reproduce it, check out commit `c82547c`, the last that
carries `scripts/perf-matrix.py` and that bundle.

## I1 benchmark

`scripts/bench-i1.py` provisions 1, 10, or 50 real worktrees with seeded,
deterministic stand-in workers through Hydra's public commands. It supports
local interactive and local headless workers. These workers do not establish
authenticated agent or remote provider qualification.

Run the fixture and observer contract checks with:

```sh
make test-bench-i1
```

Run one readiness check in a new output directory:

```sh
make bench-i1 BENCH_I1_ARGS="--output /tmp/hydra-readiness-example --stage readiness"
```

Run an individual measurement cell:

```sh
make bench-i1 BENCH_I1_ARGS="--output /tmp/hydra-measure-example --stage measure --heads 1 --case one-changing"
```

Output paths must not already exist. `--heads` accepts 1, 10, or 50;
`--case` accepts `quiescent`, `one-changing`, `all-changing`, `sustained-output`,
or `result-output`. `--mode headless` uses supervised workers. `--clients`
accepts 1 or 3; `--preview open` attaches terminal clients and is unsupported
with headless workers. Run `python3 scripts/bench-i1.py --help` for all options.
Measurement cells use a fixed 30-second steady phase; provisioning, readiness,
and teardown add time. Larger cells create substantial real workloads.

Each run retains source and binary hashes, worker receipts, results, command
records and a `report.json`; interactive runs add observer transcripts, and
measurement runs add `process-samples.json` (readiness runs write no process
samples, and a headless readiness run opens no PTY observer). The report records
errors and cleanup independently of measurement completion. An exit of zero means
the requested harness operation completed with unchanged hashes and successful
cleanup; it is not a performance acceptance gate. `--failure-observer` is a harness
self-test: it exits zero only when the run ends `incomplete`.

Fixtures use a private HOME, Hydra state, and tmux socket. Cleanup archives
completed workers' deterministic tracked edits in
`cleanup-workload-changes.json`, verifies their exact contents against the
recorded workload and committed seed, and restores only those seed files before
public head removal. Unexpected edits remain intact and fail cleanup. Output
evidence is retained. Inspect `cleanup.json` after a failed run before taking any
manual cleanup action.

## Attention corpus

`make perf-attention PERF_ATTENTION_ARGS="--out DIR"` (`scripts/attention_corpus.py`;
`--out` is required and a new directory) runs one real public workflow (exec step
succeeded, approval-wait pending) and mutates it into the annotated cases in
`tests/fixtures/perf/attention-cases.json`: blocked on approval (pending and
expired), question (with and without a request record), working, done-but-unseen and
done-but-seen, idle (empty runs and no workflow storage, both an exact empty list),
unknown (tampered binding, missing request, tampered result, and run storage that is
not a directory, which must fail explicitly) and a failed step.

It scores the items of `hydra workflow attention --json` per case: false positives
and missed signals are totalled separately, unknown items are matched by count, and
cases the local route cannot classify are reported as silent by design rather than
as misses. The `attention-data` wire is only compared by row count against the JSON
items, and the result of marking an item seen is recorded; neither gates the exit
status. The command exits 1 on a missed signal, a false positive, an explicit-error
mismatch, an unmatched unknown count, a failed cleanup or an incomplete case, which
fails the Make target. `tests/test_attention_corpus.sh` runs it under `make test`.

`python3 scripts/bench-attention.py --local-corpus --out DIR` calls the same
function but exits 0 whenever the report is `complete`, even with false positives
or missed signals in the totals it prints; use `make perf-attention`, or read the
totals, when the score matters.

## Attention and review benchmark

A separate benchmark drives two real native PTYs against the public fleet fixture
and measures attention, review and client overhead. It needs the native helpers and a
new output directory (existing evidence is never overwritten):

```sh
output_parent=$(mktemp -d "${TMPDIR:-/tmp}/hydra-attention.XXXXXX") && make build-tui build-fleet \
  && python3 scripts/bench-attention.py --tui build/hydra-tui --fleet build/hydra-fleet --out "$output_parent/run"
```

It creates private disposable fixture repositories, uses no network SSH, and fails
closed on missing evidence, identity mismatches, missed frames and failed cleanup.
`--repetitions` (at least 3), `--interval` and `--window` bound the schedule.

## Limits

Measurements come from deterministic stand-in workers on one host: they do not
establish authenticated agent or remote provider qualification, and remote
headless and disconnect/reconnect under pressure are not measured here. No macOS
baseline has been recorded; the process sampler (`scripts/bench_i1_process.py`)
supports Darwin as well as Linux.
PTY observations are not input-to-pixel measurements, latency tails need more
samples than a single run provides, and missing counters are unknown rather than
zero. See [the roadmap](ROADMAP.md#10-performance-baselines-idle-efficiency-and-change-locality);
the attention and review routes are specified in [the contracts](CONTRACTS.md).
