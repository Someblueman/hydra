# Linux x86_64 container baseline, 17 October 2026

First item 10 baseline from the [matrix runner](../../../performance-harness.md):
local worktrees with deterministic stand-in agents, measured on a shared Linux
container. It is the measurement slice on Linux only. Every number here is a
single trial on an uncontrolled, loaded host; the budgets at the end are
**Linux-container provisional**, not frozen release budgets. No performance
improvement is claimed.

## What was measured

- Product: `bin/`, `lib/` and `src/` identical to `main` at `9765ec9` (the branch
  changes only the harness, tests and docs). Harness: `61f9a72` plus the working
  changes committed afterwards as `0ecae74` and `d31b596`; the bundle fingerprints
  the working bytes of every run. Two runs (`h10-headless-1s`, `h50`) record
  `identities_unchanged: false` because harness files were edited while they ran.
- Host: Linux 6.18.44 (Firecracker VM), x86_64, 4 logical CPUs, git 2.43.0,
  tmux 3.4, dash, Python 3.13. Other agents ran builds and test suites
  concurrently; the 1-minute load average recorded around the fixtures ranged
  from 0.1 to 24.2 (most fixtures 2 to 9). Load is recorded per fixture and per run.
- Windows: settle 3 s, idle 8 s, four stimuli 1.5 s apart (10 s interaction
  window), display polling every 100 ms, process sampling every 500 ms. Producers
  write 128-byte lines every 31.25 ms into tmux panes (interactive) and every
  125 ms into retained provider stdout (headless).
- Eight runner invocations were merged with `scripts/perf_matrix_merge.py`; fixture
  ids and cell labels carry the run label. Failed fixtures stay in the bundle with
  their errors.

| Run | What it contributes |
|---|---|
| `main` | 1 and 10 heads, interactive and headless, one and three clients, preview closed and open, all four cases (the 10-head headless fixture stopped at sustained output) |
| `h10-headless-1s` | 10-head headless retry at a 1 s producer interval; same failure |
| `h10-headless-lock` | diagnostic with `HYDRA_LOCK_RETRIES=200` (non-default; flagged and excluded from budgets); same failure |
| `h10-headless-3c` | 10-head headless, three clients, quiescent/one-changing/all-changing |
| `h10-headless-output` | fourth 10-head headless sustained-output attempt, default configuration; same failure |
| `h50` | first 50-head attempt: interactive lost its cell to the post-cell view restore, headless never became usable |
| `h50-interactive` | 50-head interactive quiescent and one-changing, one and three clients, preview closed and open |
| `h50-headless` | 50-head headless retry with a 40 s readiness bound; never became usable |

## Coverage

| Heads | Mode | Measured cells | Not measured |
|---:|---|---|---|
| 1 | interactive | 10/10 planned: 4 cases x 1 and 3 clients (preview closed), quiescent and sustained output with preview open | preview open with 3 clients and for the changing cases (not planned) |
| 1 | headless | 8/8 planned: 4 cases x 1 and 3 clients | preview open (unsupported: no terminal) |
| 10 | interactive | 10/10 planned, as at 1 head | as at 1 head |
| 10 | headless | 6/8 planned: quiescent, one-changing, all-changing x 1 and 3 clients | sustained output, 1 and 3 clients: finding 1 |
| 50 | interactive | 5/5 planned: quiescent and one-changing x 1 and 3 clients, quiescent with preview open | all-changing and sustained output (not planned at 50) |
| 50 | headless | 0/4 planned | finding 2 |
| any | remote headless | none | needs a live verified fleet host; no stand-in |
| any | macOS | none | this run executed on Linux only |

Also unmeasured: disconnect/reconnect under output pressure, T2/T3 cells,
authenticated live agents, per-client frame attribution, a second trial.

## Idle cost, quiescent, one client, preview closed

Idle window after settling. CPU is percent of one core; helpers are the TUI's
reaped children (the shell data adapter and its git, tmux and hydra calls).

| Heads | Mode | TUI CPU % | helpers CPU % | launches/min | TUI RSS MiB | git per unchanged worktree per 8 s |
|---:|---|---:|---:|---:|---:|---:|
| 1 | interactive | 1.4 | 22.0 | 480 | 44.2 | 4 |
| 1 | headless | 1.3 | 16.6 | 420 | 33.5 | 4 |
| 10 | interactive | 4.3 | 97.9 | 428 | 26.3 | 2 to 3 |
| 10 | headless | 3.4 | 99.4 | 398 | 25.3 | 2 |
| 50 | interactive | 9.3 | 312.2 | 488 | 23.7 | 1 |

Three clients roughly triple helper CPU and launches (each client runs its own
adapter). Every cell, with latency, update, locality, pressure, startup and
observer-overhead tables, is in [summary.md](summary.md).

## Findings

1. **10 concurrent supervised headless runs lose their observations.** Each
   provider status line runs a synchronous `hydra adapter ingest`, bounded at 5 s;
   when one fails the run ends `observation_unavailable` (exit 125) and the worker
   is stopped. In four sustained-output attempts (125 ms twice, 1 s, and 1 s with
   `HYDRA_LOCK_RETRIES=200`) 8 to 10 of 10 runs ended this way mid-phase, so the
   cell could not finish and the fixture could not continue. In the three-client
   run without output, all phases completed but 7 of 10 runs ended this way on
   their final, simultaneous observation. At one head every run completed. The
   public result does not keep the failing call's reason (timeout or non-zero
   exit), so the cause is not isolated here. **Resolved after this baseline**:
   see the [addendum](#addendum-17-october-2026-10-head-headless-sustained-output-after-the-observation-fix).
2. **50 headless heads never became usable.** In two attempts (15 s and 40 s
   readiness bounds) the first client showed "STALE: no snapshot yet / shell data
   adapter timed out" and no heads.
3. **Cancel misses its deadline at 50 heads.** Returning to the Work view took
   3.9 s and 2.0 s in two cells and missed the 4 s deadline in three; one search
   also missed. At 1 and 10 heads every stimulus was observed (cancel max 480 ms).
4. **The changed-file count lags or stalls with scale.** Selected-head update
   latency median 1.4 s at 1 head and 2.0 s at 10 heads (max 3.7 s); at 50 heads
   none of 18 declared changes appeared within the window.
5. **No change locality today.** An unchanged worktree gets as many git launches
   per idle window as the changed one, and the count is the same with no change;
   the per-worktree rate falls with scale only because each refresh takes longer
   (adapter wall time up to 2.6 s at 10 interactive and 5.2 s at 10 headless
   heads). Idle helper CPU grows from about
   0.2 core at 1 head to about 1 core at 10 and 3.1 cores at 50.
6. Sustained output at 4 KiB/s per producer left no producer backlog at the
   declared end (35 receipts, 0 EAGAIN); tmux resident memory grew up to
   23.2 MiB over 18 s at 10 interactive heads.

## Addendum, 17 October 2026: 10-head headless sustained output after the observation fix

Not part of the bundle above (which is unchanged) and not used for its budgets.

Cause of finding 1, isolated afterwards with a deterministic reproduction
(`make test-one T=concurrent_headless_observation`): each status ingest waited
for the shared project state lock for 5 tries of 50 ms and then failed, and the
CLI reported that as "unsupported adapter status 'running'". With
`HYDRA_LOCK_RETRIES=200` the ingests queued behind one another instead and hit
their 5 s deadline (exit 124), because every provider status line (8 per second
per head) started one. Before the fix that reproduction kept 2 of 10 and 1 of
20 concurrent runs under three `yes` burners.

The fix records a status only when it changes within a run, keeps a lost one in
the receipt (`diagnostic.observation`) without stopping the worker, retries it
no sooner than 1 s later and once after the provider exits, gives observed
status a bounded 2 s lock window and shortens the locked section. The
reproduction then passed three consecutive times at 10 heads and at 20 heads
with three `yes` burners (load average 1.8 to 10.7 at start); at 20 heads 1 to
4 runs per pass needed a retry, and every final status was recorded.

Rerun of the planned 10-head headless sustained-output cells, default
configuration, 125 ms producer interval, one trial, at `6480418` (identities
unchanged), load average 3.2 at start and 5.4 at end:

```sh
make perf-matrix PERF_MATRIX_ARGS="--output DIR --heads 10 --modes headless \
  --clients 1 3 --preview closed --cases sustained-output --settle 3 --idle 8 \
  --gap 1.5 --poll-ms 100 --load-note '...'"
```

Cells planned 2, measured 2; validation ok; cleanup ok; 0 fixture errors. All
10 supervised runs exited 0 with state `completed`, no receipt recorded a lost
observation, and result retention was verified (288 output events per worker).

| Cell | TUI CPU % | helpers CPU % | launches/min (git/tmux/hydra) | TUI RSS MiB | nav / search / resize / cancel ms | peak backlog B | EAGAIN | recovery ms |
|---|---:|---:|---|---:|---|---:|---:|---:|
| 10/headless/1c/closed/sustained-output | 4.13 | 120.29 | 735 (91/0/7) | 37.0 | 0.6 / 4.6 / 15.5 / 20.9 | 128 | 0 | 0.0 |
| 10/headless/3c/closed/sustained-output | 7.88 | 264.19 | 1530 (186/0/18) | 95.8 | 0.7 / 1.9 / 30.0 / 20.6 | 128 | 0 | 0.0 |

Provisioning took 27.8 s (spawn median 1.9 s) and clients were usable after
2.0 to 2.3 s. Finding 2 (50 headless heads) is not addressed. Starting many
supervised runs at the same instant is a separate limit: the host admission
lock (40 tries of 50 ms) refused 1 of 10 and 14 of 20 simultaneous `exec`
starts in a side experiment, so the reproduction starts owners one at a time,
as this fixture does.

## Provisional budgets (Linux container; not frozen)

Derived as 1.5 times the observed maximum, from default-configuration cells only,
on this host at the load above. They exist to detect regressions until a macOS
baseline exists and a decision freezes budgets. Tails are maxima: no per-scale
stimulus reached the 20 samples p95 needs.

| Budget | 1 head | 10 heads | 50 heads |
|---|---:|---:|---:|
| Idle TUI CPU, % of one core | 2.1 | 6.4 | 13.9 |
| Idle helper CPU, % of one core | 33.0 | 149.1 | 468.3 |
| Idle helper launches per minute | 720 | 765 | 731 |
| TUI resident memory, MiB | 66.4 | 41.5 | 35.6 |
| Navigation, ms (preview-open path dominates) | 355 | 354 | 338 |
| Search, ms | 12.5 | 6.3 | 9.0 (1 miss) |
| Resize, ms | 47.3 | 57.7 | 37.6 |
| Cancel, ms | 42.1 | 720 | violated: 3 misses |
| Selected-head update latency, ms | 3068 | 5618 | violated: none seen |
| Producer backlog after output stops, ms | 0 | 0 | not measured |
| git launches per unchanged worktree per 8 s idle window (observed maximum) | 4 | 3 | 1 |

Deadline misses must stay at zero. The locality row is today's cost, not the
target: the target is no repeated scan of an unchanged worktree. tmux growth
under output is recorded per cell but not budgeted from one trial.

## Files

- `summary.md`: every measured cell, with unsupported and unmeasured cells and
  their reasons, the attention corpus result (13 cases, 0 false positives, 0
  missed signals) and the proposed budgets as JSON.
- `bundle.json.gz`: the compact `hydra-perf-matrix/1` bundle (revisions,
  hashes, seed 104729, tool versions, clock bounds, shim overhead, per-window role
  summaries, stimuli, locality, pressure, workload proofs, per-run commands and
  load). `zcat bundle.json.gz | python3 -m json.tool` reads it.
- `raw/`: gzipped timestamped process samples for 31 of 45 measured cells,
  smallest first within a 1.45 MB budget (the three non-default diagnostics are
  never published); the bundle's `raw_samples_published` lists each omitted cell
  and why.

## Reproduce

```sh
make perf-attention PERF_ATTENTION_ARGS="--out /tmp/hydra-attention"
make perf-matrix PERF_MATRIX_ARGS="--output /tmp/hydra-perf --heads 1 10 \
  --settle 3 --idle 8 --gap 1.5 --attention /tmp/hydra-attention/report.json"
make perf-matrix PERF_MATRIX_ARGS="--output /tmp/hydra-perf-50 --heads 50 \
  --modes interactive headless --cases quiescent one-changing --settle 3 --idle 8 --gap 1.5"
python3 scripts/perf_matrix_merge.py --run main=/tmp/hydra-perf \
  --run h50=/tmp/hydra-perf-50 --output /tmp/hydra-perf-merged --evidence-dir DIR
```

The exact command of every run is in the bundle (`runs[].command`).
`make test-perf-matrix` checks the runner, the schema and the smallest cells; it
asserts no timing.
