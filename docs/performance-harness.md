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

## Matrix runner

`scripts/perf-matrix.py` (Python 3 standard library only) drives the same fixture
across the item 10 matrix: 1, 10 and 50 heads; quiescent, one-changing,
all-changing and sustained-output cases; one or three attached native clients;
local interactive and local headless workers; preview closed or open. Each
(heads, mode) fixture is provisioned once and every client group and case reuses
it, so a cell is a named worker phase (`p1`, `p2`, ...) with its own settle,
idle and interaction windows. Remote headless and macOS cells are recorded as
unmeasured with a reason; they are never silently skipped.

```sh
make perf-matrix PERF_MATRIX_ARGS="--output /tmp/hydra-perf --heads 1 10 --evidence-dir docs/evidence/performance/<date>-<platform>"
make perf-attention PERF_ATTENTION_ARGS="--out /tmp/hydra-attention"
make test-perf-matrix
```

`scripts/perf_matrix_merge.py --run LABEL=DIR ... --output DIR --evidence-dir DIR`
joins several runner invocations into one validated bundle and summary, with the
proposed budgets recomputed over every measured cell; fixture ids carry the run
label and each run's command, load and producer interval are kept.

`python3 scripts/perf-matrix.py --help` lists every option. `--settle`, `--idle`
and `--gap` set the window lengths (idle plus the four-stimulus interaction window
must fit the worker's 30 s phase bound); `--trials` repeats cells and, with
`--candidate-source`/`--candidate-build`, alternates baseline and candidate
fixtures per trial; `--output-interval-ns` changes the declared 31.25 ms producer
line interval (128-byte lines); `--fixture-env KEY=VALUE` runs the fixture's public
commands, owners and workers under a non-default configuration (recorded as
`declared.fixture_env`; say why in `--load-note`). `--attention REPORT` embeds a
`perf-attention` report; `--evidence-dir` publishes `summary.md`, a compact `bundle.json` and
gzipped raw samples bounded by `--raw-budget-bytes`.

What each cell measures:

- **Idle cost** after a sampled settle window: CPU seconds, CPU percent of one
  core and of the machine, voluntary context switches per second (a wakeup
  proxy), resident and peak memory, descriptor, thread and process counts, per
  role: `native-tui#N`, `pty-observer#N`, `tmux-server`, `pane-shell`, `agent`,
  `execution-owner` and `controller`. Helper CPU is the launcher's reaped-children
  CPU from `/proc`, a lower bound that misses unreaped work. System-wide forks
  per second come from `/proc/stat` and include every other process on the host.
- **Helper launches** through PATH shims for `git` and `tmux`, and wrapping
  scripts for the `hydra` CLI, `hydra-core` and `hydra-fleet`. Each launch records
  its ancestry so it is attributed to the launching role, to a worktree
  (`git -C`) or to a session (`tmux -t`); wrapped helpers also record their exit,
  giving adapter wall time. Utilities that are not shimmed (`awk`, `sed`,
  `find`, ...) are not counted. The shim's own cost (one dash process per launch,
  about 1.5 ms here) is calibrated per fixture and stored as observer overhead.
- **Interaction latency**: navigation, search, resize and cancel stimuli on an
  absolute schedule during the interaction window, each with a declared applied
  region and a 4 s deadline. The value is scheduled or injected input to the
  first complete PTY frame containing the region; it is not input-to-pixel.
  Medians are always reported; p95 needs 20 samples and p99 needs 100, otherwise
  they stay null with the reason. Misses, late dispatch and invalid predicates
  are retained.
- **Update latency**: in changing cases the selected head's "N changed files"
  line is polled every `--poll-ms` while the worker writes timestamped edits; the
  latency is the worker receipt to the first polled frame showing the new count,
  with a lower bound from the previous poll (exact when a single PTY read
  occurred between polls). The Work view shows no per-head count for unselected
  heads, so their update latency is recorded as not applicable, and the
  detection/collection/model/render split is unknown because the native TUI
  exposes no stage timestamps.
- **Change locality**: git launches per unchanged worktree and per changed
  worktree, tmux captures per unchanged session, and adapter runs in the idle
  window.
- **Output pressure**: declared producers, bytes per second and line size, the
  producers' peak backlog, EAGAIN count and queue-non-empty time, time to drain
  after the declared end, resident growth per role, and PTY reads per client.
  Dropped or coalesced display updates are null: the renderer repaints on its
  period and exposes no such counter.
- **Startup**: public spawn durations, launch-to-worker-ready, and per-client
  time to usable (raw mode and all owned heads visible). Headless fixtures also
  record result retention time: the supervised owner retains every observation
  event after the worker exits, which takes roughly 0.1 s per event on this host.

The attention qualification (`scripts/perf_matrix_attention.py`, also reachable
as `python3 scripts/bench-attention.py --local-corpus --out DIR`) runs one real
public workflow (exec step succeeded, approval-wait pending) and mutates it into
the annotated cases in `tests/fixtures/perf/attention-cases.json`: blocked on
approval (pending and expired), question (with and without a request record),
working, done-but-unseen and done-but-seen, idle (empty runs and no workflow
storage), unknown (tampered binding, missing request, tampered result) and a
failed step. `hydra workflow attention --json` and the `attention-data` wire are
scored per case; false positives and missed signals are totalled separately,
unknown items are matched by count, and cases the local route cannot classify
are reported as silent by design rather than as misses. Marking an item seen is
checked to keep the public signal.

Evidence bundles (`bundle.json`, schema `hydra-perf-matrix/1`) keep exact
revisions and runtime, harness and binary hashes, the workload seed, tool
versions, declared policy constants, clock calibrations, attribution boundaries,
shim overhead, per-window role summaries, per-cell stimuli, update, locality and
pressure records, workload proofs, unsupported cells with reasons and the
proposed provisional budgets. Unsupported counters stay null with a reason;
`validate_bundle` rejects nulls without one. Published copies under
`docs/evidence/performance/` are compacted (per-process rows, polled frames and
runtime file hashes remain in the run directory, replaced by digests and counts).

## Qualification limits

This harness is a bounded measurement tool, not completion of roadmap item 10.
The earlier Mac 1/10/50 quiescent observations and selected single-worker output
observations remain historical, source-specific evidence. The Linux container
baseline under `docs/evidence/performance/2026-10-17-linux-x86_64/` covers the
local matrix with deterministic stand-ins on a shared 4-core host (its `README.md`
lists the cells, the observed load and the provisional budgets); remote headless,
macOS, disconnect/reconnect under pressure and authenticated-agent qualification
remain outstanding.

Helper accounting is exact only for shimmed helpers and reaped children;
latency tails need more samples than a single run provides; the proposed
budgets are provisional Linux-container numbers, not frozen release budgets.
Missing counters are unknown. PTY observations are not visible-terminal
input-to-pixel measurements. No speedup is claimed. See
[the roadmap](ROADMAP.md#10-performance-baselines-idle-efficiency-and-change-locality)
and [the separate attention qualification](attention-review-acceptance.md).
