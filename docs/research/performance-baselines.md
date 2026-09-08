# Hydra performance measurement research

Research date: 8 September 2026. Proposed work is tracked in
[roadmap item 10](../ROADMAP.md#10-performance-baselines-idle-efficiency-and-change-locality).
This note defines measurement boundaries; it contains no new benchmark results.

## Existing coverage and the gap

`make bench`, `make bench-core`, and `make bench-tui` already provide benchmark
entry points. `scripts/bench-tui.sh` uses isolated live tmux sessions and a fixture
map at 5, 20, and 100 heads. It measures adapter refresh, ten headless frames,
startup, and a short interactive PTY CPU window. These sessions do not establish
1/10/50 real worktrees running active agents. Preview is excluded from that scope.
See [native TUI qualification](../NATIVE_TUI.md).

The current `src/tui/input.c` loop refreshes the model periodically, and
`src/tui/adapter.c` launches the snapshot subprocess. These are candidates for
measurement, not proof that any particular replacement will be faster or correct.
Startup/render timings cannot establish steady-state idle cost, change freshness,
tail responsiveness, or memory stability during sustained output.

## Research findings and their application

- Apple's [Mac energy-efficiency guidance](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/Timers.html)
  explains the cost of timer wakeups and recommends event notifications where
  suitable. For Hydra, measure wakeups and periodic work independently of CPU
  averages; consider notifications without assuming they eliminate reconciliation.
- Git's [filesystem monitor documentation](https://git-scm.com/docs/git-fsmonitor--daemon)
  describes avoiding full working-directory scans by obtaining a summary of changes.
  This supports investigating change-based invalidation. It does not justify making
  fsmonitor a Hydra dependency or assuming availability on every host/filesystem.
- [HdrHistogram's latency recording API](https://hdrhistogram.github.io/HdrHistogram/JavaDoc/org/HdrHistogram/Histogram.html)
  addresses coordinated omission: a stalled system can suppress observations and
  make reported latency look better. Drive timed input independently of render
  completion and record scheduled versus actual injection times, queueing, and
  missing responses. A histogram library is optional; accurate sampling is not.
- Linux [Pressure Stall Information](https://docs.kernel.org/accounting/psi.html)
  measures CPU, memory, and I/O contention, including cgroup scope where supported.
  Use it as optional explanatory evidence during output/scaling trials. It is not
  a portable replacement for latency, nor an interchangeable macOS counter.

These sources motivate the following Hydra-specific design; they do not establish
universal numeric budgets or measured improvements in Hydra.

## Experiment contract

Use 1, 10, and 50 real worktrees with declared tracked/untracked file counts,
repository sizes, agent profiles, and local/remote placement. At each size run:

| Scenario | Stimulus | Main question |
| --- | --- | --- |
| Settled idle | Active agents waiting, no output or repository mutations | What does Hydra do with nothing to update? |
| One changed worktree | Repeated tagged file/status/output changes in one tree | How much unrelated work does each change cause? |
| All changing | Controlled activity in every tree | How do overhead and freshness scale? |
| Output pressure | Concurrent paced bursts and sustained streams | Do input, memory, and recovery remain bounded? |

Use deterministic stand-ins for repeatable workload rates, then separately qualify
representative authenticated agents. Record actual concurrency and provider waits;
do not silently substitute fifty queued tasks for fifty active agents. Include
native and basic TUI modes with their supported interactions, preview on/off,
and a no-TUI control. Use isolated disposable fixtures and explicit host capacity.
Do not change the user's running agents, admission policy, or global power settings.

Record process-tree CPU seconds and wall duration (100% means one core), wakeup
counter semantics, launch counts, memory accounting, scans, redraws, and I/O.
Separate Hydra/helper cost from agent and tmux cost, while also reporting the whole
scenario. Account for short-lived and detached helpers; a final PID snapshot misses
them. Record background host load and a matched observer-only control. RSS summed
across processes may double-count shared pages; label it rather than calling it
unique physical memory. Instruments on macOS and perf/proc counters on Linux are
candidate tools, subject to supported counters and permissions. Unsupported wakeup
measurements are unavailable, not inferred from context-switch counts.

Timestamp input injection, change creation, detection, collection completion,
model application, and matching frame output using a monotonic clock per host.
Use tagged stimuli to match the intended state rather than accepting any redraw.
A PTY observes output bytes, not terminal pixels: supplement it with presentation
measurement on a declared terminal when claiming visible input-to-render latency.
For remote updates, report clock-offset uncertainty or use a same-clock controller
round-trip boundary with its transport contribution explicitly included.

Keep raw samples, sample counts, run durations, missed responses, and observed
maxima. Obtain enough independent trials and tail observations for the stated
percentiles; do not hide stalls by waiting for completion before scheduling the next
input. Separate cold startup, warmed steady state, and post-burst recovery. Alternate
baseline/candidate order and record machine, OS, power/thermal conditions, terminal
geometry, compiler flags, binaries, workload seed, and profiler overhead.

## Locality and acceptance

For each scale N, report idle cost I(N) and extra work D(N) caused by a fixed number
of changes in one tree. Attribute D(N) to that tree, unchanged trees, and shared
work. Count Git scans, subprocesses, capture operations, bytes read, and redraws
as well as elapsed time. The desired result is a small idle floor and incremental
work governed primarily by changed state, rather than total worktree count.
Document legitimate shared-ref invalidation, host health checks, and recovery scans.

Test file replacement/rename, shared Git ref changes, output bursts, watcher loss
or overflow where applicable, and disconnect/reconnect. An optimization fails if
it lowers CPU by missing changes or leaving the display indefinitely stale. Bound
display buffering and recovery time, disclose display coalescing, and retain exact
task artifacts and required evidence. Measure whether slow readers block producers.

First publish baselines and choose explicit budgets for idle overhead, latency
tails, memory growth, backlog recovery, and unrelated-tree operations on supported
reference machines. Freeze those budgets before candidate evaluation. Keep absolute
user-facing limits and baseline-relative regression checks distinct. A noisy run
is inconclusive, not a pass; existing short-window CPU ceilings do not define
the new near-idle target. Only then select and qualify the smallest measured fix.
