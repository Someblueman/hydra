# Native statistics

Open `hydra tui --view statistics` or press `D` from any native view. Press `D`
again to return. Workspace selection, pane focus and scroll remain intact.
The view reads existing records; it neither starts work nor changes runtime state.

## Controls

| Key | Action |
| --- | --- |
| `M` | Cycle overview, queue delay, total execution, recorded verification, owner recoveries (local) |
| `T` | Cycle all recorded, last 24 hours and last 7 days (local mode) |
| `[` / `]` | Filter by workflow name (local mode) |
| `/` | Search workflow, run ID or project; fleet mode searches host names |
| `!` | Toggle attention filter |
| `0` | Reset filters |
| `j` / `k`, arrows, row click / wheel | Select a contributing run or host |
| `Enter` | Open/close its recorded step or head evidence |
| `j` / `k` in evidence | Scroll contributing records |
| `g` | Open selected run in the existing workflow graph |
| `Esc` | Return from graph/evidence, then to the previous view |
| `r` | Refresh observations |
| `q` | Exit |

The statistics view has independent filters and selection. Changing filters leaves
no hidden denominator: cards, outcomes, timing and run rows use the same matched
run cohort and its recorded steps. Local search is case-insensitive; host search
is case-sensitive. Wide terminals expose charts and coverage; compact terminals
prioritize counts, selection and evidence. The minimum supported size is 40x10.

## Definitions and limits

- **Runs:** matched records from the current project's workflow run directory.
  Time filters use run creation time against the snapshot time, not completion
  time. All-recorded includes undated runs; time filters exclude and count them.
- **Outcomes:** recorded run or step state. `ready`, `retrying` and `queued` map
  to queued; `recovery-required`, `blocked` and `waiting-approval` map to blocked.
  Stale/unrecognized/missing states map to unknown. Attention includes failed,
  blocked and unknown runs. A recorded success is not a verified-result claim.
- **Running:** recorded running steps in the selected cohort. This is not an
  independent process-liveness measurement.
- **Retries:** sum of `max(attempts - 1, 0)` over steps with known attempt counts.
  Attempts coverage reports known steps / recorded steps. Partial coverage is
  not extrapolated; no known counts displays `--` rather than zero.
- **Latest attempt mean/max:** seconds between a step's latest start scalar and
  its current attempt's recorded completion scalar. Only known, positive attempt
  numbers with valid timestamps contribute. Missing, future, reversed or
  pre-run timestamps do not contribute. A valid zero-second interval is zero.
  `n` and timing coverage expose the denominator. These are neither total run
  duration nor a complete history of every retry.
- **Queue delay:** first `initial-ready-at` to `initial-started-at`, in seconds.
  Denominator: recorded non-approval-wait steps, excluding those known to have
  zero attempts and no first start. Missing attempt evidence leaves eligibility
  uncertain and is conservatively counted as unknown. Dependency waiting and
  retry backoff are excluded. Queue delay measures scheduling after readiness.
  Start means coordinator dispatch; downstream host-admission waiting is included
  in total run duration, not measured as a separate queue sample. Task steps whose
  first-dispatch boundary is not recorded remain unknown.
- **Total execution:** run `started-at` (first coordinator drive) to `completed-at`
  (terminal result), in seconds. Denominator: succeeded, failed and cancelled runs.
  Includes child execution, dependency waits, approval waits, retry backoff and
  downtime before owner recovery. Resumable recovery-required runs are not terminal.
- **Recorded verification:** first drive to the independent plan finish gate's
  successful `verified-at`. Denominator: runs with an accepted compiled plan,
  including failed and unfinished plans as unknown. Only succeeded runs with a
  matching accepted-plan digest contribute. This records when verification passed;
  it does not claim the artifact is still valid. `workflow plan result` checks
  present artifact integrity again. Normal workflows are not eligible.
- **Owner recoveries:** accepted coordinator resumptions under the run drive lock.
  Denominator: all matched runs. New runs begin at zero. Approval continuation and
  automatic step retries do not increment it; stale-owner takeover does. A rejected
  resume is not counted. Missing historical counters remain unknown.
- **Metric pages:** mean, maximum, and nearest-rank p50/p95 use known samples only;
  coverage is known / eligible, without extrapolation. Timing values are seconds;
  recovery values are counts. Seven rolling 24-hour bins show means and known
  sample sizes grouped by run creation, using the same filters. Empty bins show
  `--`. At compact sizes the distribution summary and selected evidence remain
  available; charts require at least 80x24. `Enter` and `j/k` inspect the selected
  run's step values for queue delay. Each page reports snapshot age.
- **Run creation chart:** seven rolling 24-hour bins within the matched cohort,
  relative to the snapshot timestamp. It does not measure throughput.
- **Fleet:** current host list responses, failed observations and known reported
  head counts. Failed hosts show unknown counts; successful empty responses show
  zero. Host evidence lists that host's reported heads. Remote workflow timing,
  CPU, memory, tokens, cost, queue delay and time to verified result are unavailable.

The local feed samples at most 128 runs in ID order, 128 steps per run and 1024
steps overall. This is a bounded sample, not guaranteed to be the newest 128 runs.
Within the sample, the UI sorts rows by creation time. Limit/missing-record warnings
remain visible. The graph feed has separate, smaller limits; a selected run outside
that graph sample produces an explicit notice and remains inspectable in statistics.

Scalar files are individually observed, not a transaction across the run directory.
An attempt-counter change during collection discards that step's latest timing/counts.
A state or recovery-counter change while collecting run metrics discards those
run metrics. These guards cannot provide a transactional snapshot across all files.
Symlinked runs, step directories and scalar files are skipped or reported unknown.
A running run with a stale owner is classified unknown. Missing numeric evidence
never becomes zero. A malformed or timed-out refresh retains the last valid sample
with a stale warning; without one, statistics are unavailable. Native collection
has a two-second subprocess timeout, so large/slow repositories can be unavailable
instead of displaying a truncated sample as complete.

## Read-only projection

`hydra workflow statistics-data` is a versioned native-view TSV feed, separate from
`workflow tui-data`; existing graph consumers keep their original contract. Fields
are tab-separated; `-` is missing scalar evidence. Each record ends in a newline.

```text
HYDRA_STATISTICS  2  snapshot_epoch_seconds
R  run_id  workflow_name  state  project_id  created_at_utc  complete|partial  first_drive_epoch  terminal_epoch  verified_epoch  recovery_count  compiled_plan_0_or_1
S  run_id  step_id  kind  state  attempts  latest_started_epoch  latest_completed_epoch  first_ready_epoch  first_started_epoch
X  coverage_warning
Z  run_count  step_count
```

`R` precedes its `S` records. Creation timestamps use `YYYY-MM-DDTHH:MM:SSZ`.
The required final `Z` counts detect truncation. The C reader rejects unsupported
versions, duplicate identities, malformed framing/counts, oversized fields and
streams above 1 MiB. Invalid numeric/date evidence remains unknown. This feed is
an internal native-view boundary, replaced together with its native consumer.

New durable scalar fields are additive within existing workflow runtime version 1:
run `started-at`, `completed-at`, `verified-at`, `verification-plan-sha256`, and
`recovery-count`; step `initial-ready-at` and `initial-started-at`. Existing state,
event, attempt, approval and plan contracts retain their meaning. Older records
are not backfilled from latest-attempt times or inferred from success; missing
boundaries remain unknown. Timestamps have one-second wall-clock resolution;
clock changes can invalidate intervals, and valid same-second intervals are zero.
Statistics persistence is optional: a failed run-start or recovery-counter write
does not block execution, and failed verification-timing writes do not change a
verified delivery into failure. Partial timing records remain unknown; the
accepted-plan binding is published only after its timestamp. Required owner,
execution-state and independent-verification writes remain execution gates.

## Local qualification

```sh
make test-statistics
make test-visualization
make sanitize-statistics
make test-all
```

The fixed eight-run/eleven-step fixture reconciles filters, every outcome category,
three retries, eight known attempt counts and six known durations (mean 95 seconds,
maximum 120). Malformed framing, missing numbers, dates and zero-duration boundaries
have direct C checks. PTY tests exercise filters, evidence/graph return, workspace
scroll preservation, `a` switch dispatch, mouse selection, failed-refresh retention,
fleet empty/failed separation, 140x40/80x24/40x10 resizing and terminal restoration.

The real-workflow test executes a four-step workflow, checks its read-only statistics
projection against recorded timestamps and verifies state checksums are unchanged.
It also removes timing evidence and corrupts an attempt counter to verify unknown
coverage. Evidence is generated under `build/visualization-evidence/`; illustrative
fixture terminal captures are under `build/statistics-evidence/`. They establish
separate facts: the fixture tests presentation and reconciliation; the real run
checks the actual shell-to-native path. Attached-session and exact-plan-approval
tests now cover D round trips separately; see [attached terminals](ATTACHED_TERMINALS.md).
Real agent-authored workflow and additional-platform qualification remain open.

Real retry/recovery, approval continuation, positive and negative plan checks
compare the shell feed and native aggregates with the run's actual scalar records.
Missing historical boundaries and counters are tested separately. The metric-page
PTY checks exercise all four pages at 40x10, 80x24 and 140x40, including filters,
evidence and workspace return. These are local workflow measurements, not provider
cost/resource measurements or fresh remote/provider campaign qualification.
