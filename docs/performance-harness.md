# Performance measurement harness

The item 10 harness provisions 1, 10, or 50 real worktrees with seeded,
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

Each run retains source and binary hashes, process samples, worker receipts,
results, observer transcripts, command records, and a `report.json`. The
report records errors and cleanup independently of measurement completion.
An exit of zero means the requested harness operation completed with unchanged
hashes and successful cleanup; it is not a performance acceptance gate.

Fixtures use a private HOME, Hydra state, and tmux socket. Cleanup archives
completed workers' deterministic tracked edits in
`cleanup-workload-changes.json`, verifies their exact contents against the
recorded workload and committed seed, and restores only those seed files before
public head removal. Unexpected edits remain intact and fail cleanup. Output
evidence is retained. Inspect `cleanup.json` after a failed run before taking any
manual cleanup action.

## Qualification limits

This harness is a bounded measurement tool, not completion of roadmap item 10.
The earlier Mac 1/10/50 quiescent observations and selected single-worker output
observations remain historical, source-specific evidence. Linux coverage is
partial; remote and authenticated-agent qualification remains outstanding.

Complete helper/descendant accounting, idle budgets, update freshness, robust
latency tails, change locality, reconnect behavior, and the full platform/client/
workload matrix remain open. Missing counters are unknown. PTY observations are
not visible-terminal input-to-pixel measurements. No speedup or performance
budget is claimed. See [the roadmap](ROADMAP.md#10-performance-baselines-idle-efficiency-and-change-locality)
and [the separate attention qualification](attention-review-acceptance.md).
