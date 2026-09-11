# V2, 9C and H2 local integration acceptance

Qualified on 10 September 2026 (Europe/London) in `codex/release-next`.
This extends the [previous integration](next-wave-integration.md) at `5fdb0e1`.
The unrelated lifecycle-quality commit `cad0403` is preserved.

| Milestone | Qualified source | Local integration |
| --- | --- | --- |
| 9C structured verifier evidence | `7e48d76` | `200a342` |
| H2 reviewed enrollment and reconciliation | `0ceebfa` | `a0ad7f7` |
| V2 bounded event and attempt observations | `e48baf5`, corrected by `f52dec3` | `79d94b5`, then `f52dec3` |

The primary agent inspected the returned code, repaired concrete acceptance
failures, and checked the combined result. One independent reviewer closed the
original identity, reconciliation, evidence and observation findings, including
the final cursor correction. Its report remains with the leader roster outside
the repository.

## Final observation correction

A two-event page correctly reported cursor 2 and stream head 13, but the separate
head lookup moved the returned byte offset to EOF. Reconnecting with the paired
cursor, byte offset and stream ID silently omitted events 3 through 13. The
correction captures the resume offset before seeking to inspect the stream head.
The public regression fails with the original binary and passes after rebuilding.

The existing task workflow now makes its work step fail with exit 7 and succeed
on its second attempt. Assertions inspect both real attempt records, their
completion timestamps, exact artifact inventory, process outcome, collection
state and recorded integrity metadata. The two-page reconnect checks every
sequence, and the existing large-stream cases cover 201 events, bounded scans,
append, malformed/duplicate sequences, invalid offsets and stream replacement.

Six additional native observations use copied retained task/run state: intact,
missing attempt, unavailable graph, zero attempts, 10,000 recorded attempts and
missing result. All pass. History is capped at 512 entries with explicit
truncation; missing result evidence leaves verification unavailable while the
successful process outcome remains separate. These are retained-state controls,
not new executions. The normal and UBSan binaries both recover all 14 events of
the real retry workflow across two pages.

## Combined checks

All completed processes listed here exited zero; final source is `f52dec3`.

| Check | Result | Local log |
| --- | --- | --- |
| Native core/fleet/TUI builds; native plan and fleet tests | Pass | `completion-final-build.log`, `completion-cursor-build.log` |
| Full public task acceptance, including real retry and cursor regression | Pass | `completion-accepted-task.log` |
| Enrollment client, receiver, strict OpenSSH and discovery | 16 + 6 + 2 + 5 passed | `completion-accepted-enrollment.log` |
| Native TUI input and shell acceptance; real PTY tests | Pass, 85/85 and 138/138 | `completion-final-tui.log` |
| UBSan native plan/fleet and corrected observation probes | Pass | `completion-final-ubsan.log`, `completion-v2-cursor-after.json` |
| Structured executable and assessment evidence through public workflows | 14 controls passed | `completion-accepted-9c.log` |
| ShellCheck and dash syntax | Pass | `completion-accepted-lint.log` |
| Full C quality with clang-tidy 22.1.8 | 186 advisory functions, no baseline regression | `completion-accepted-quality.log` |

The task fixture released all 24 owner locks and retained no active fixture
processes. `completion-fixture-final.json` records the independent cleanup check.


## Evidence and limits

Local logs and probe records are under `build/completion-*`. The cursor
before/after records are `completion-v2-cursor-{before,after}.json`; the six
retained-state probes are `completion-v2-probe.json`.

See [9C evidence](structured-evidence-9c.md),
[H2 evidence](h2-reviewed-enrollment.md), and
[the observation contract](../FLEET_OBSERVATIONS.md) for contract details and
slice-specific checks. The equality adapter and assessment records have bounded
positive/negative controls; no automatic LLM judge or calibration is claimed.
Observation polling reports retained integrity metadata without rechecking
bundles or claiming domain acceptance. Enrollment qualification uses disposable
controlled receivers and strict loopback OpenSSH, not operational hosts.

This is local implementation and qualification. It is not a full `make test-all`
run, a new live-provider campaign, hosted CI, publication, main merge or release.
Existing unrelated untracked files and worktrees are preserved.
