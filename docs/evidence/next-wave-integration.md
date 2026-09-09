# Next-wave local integration acceptance

Integrated on 2026-09-09 from `codex/hydra-next-wave`.

| Slice | Included branch tip |
| --- | --- |
| T1 terminal-independent execution | `0ad36ba` |
| 9A outcome-obligation planning | `8b32020` |
| V1 remote execution visibility | `d9add6e` |
| H1 selected SSH host discovery | `474ae0f` |
| T2 remote headless execution | `5f546e6` |
| 9B versioned handoff contracts | `b1a08d6` |

The H1/9B Makefile conflict was resolved by retaining both discovery coverage and
handoff-contract targets. The combined native build passed.

## Integration and cleanup corrections

- The domain dispatcher confused an unhandled command with a handled command
  returning `NULL` after writing raw output. Collection candidates therefore
  printed their valid row followed by an unrelated option error. Commit
  `745c31e` separates dispatch recognition from the optional JSON response.
- Normal Hydra teardown preserves dirty tracked worktrees. Disposable tests now
  separately close only their fixture-owned tmux sessions, by stable session ID
  and exact directory ownership, before deleting fixture files. Cleanup failures
  propagate, and failure evidence can remain without retaining terminal sessions.
- A one-second execution deadline can terminate an admission writer while it
  holds the admission lock. The original failed run created that lock one second
  after execution started; its next task failed with `state_unavailable`.
  Destructive deadline cases now use separate receiver homes. Production
  conservative lock retention is unchanged.
- Recorded owner PIDs can be reused during a long test. Teardown now waits on
  the receiver's actual `owner.lock` flock. Its regression holds that lock, then
  releases it while the recorded PID remains alive, proving the distinction.
- The cancellation fixture allows its existing 60-second startup budget before
  requiring a live command marker. Dashboard pane allocation fails promptly on
  allocation failure or no progress; signal traps exit through cleanup.

## Combined local checks

All listed checks passed, including teardown where applicable:

| Check | Result |
| --- | --- |
| Native core, TUI and fleet build | Pass |
| Discovery | 5 tests |
| Handoff contracts, including supervised consumer rejection | 22 tests |
| Native plan, workflow data, scheduling, fleet and task-package checks | Pass |
| Full public task acceptance over controlled SSH transport | Pass |
| Fixture cleanup: success, failure, INT, TERM, HUP, allocation and owner locks | Pass |
| Interactive compiled plan | 91/91 |
| Headless execution and compiled adapter plan | 18/18 and exact-artifact handoff pass |
| Remote DAG with lost acknowledgment; remote compiled plan | Pass |
| Workflow schema; workflow runtime; maintenance | 36/36; 47/47; 10/10 |
| Fleet CLI; fleet installation; completion | Pass; 19/19; 179/179 |
| Native core/TUI and real PTY tests | Pass |
| ShellCheck and dash syntax | Pass |
| C quality gate and analysis | Pass; 187 advisory functions, no ceiling regressions |
| UBSan plan/data, contract and discovery checks | Pass; supervised contracts covered by the normal build |
| Final raw candidate output with normal and UBSan binaries | Identical single TSV row; exit zero |

The final task fixture exited zero, released all 24 receiver owner locks, and
retained no tmux sessions. Local logs are in `build/next-wave-acceptance/`.
These checks do not add live-provider or external-host qualification beyond the
individual slice evidence, and are not a publication or release claim.

## Release branch ancestry

The old release tip `a93879c` and the already-merged PR #71 squash `26fc726`
have the identical tree `069c351090b605de29b7391654295b839f8c1c39`.
`26fc726` is an ancestor of this integration. The release history can therefore
be reconciled without changing the current tree: the older source layout must
not be resurrected or merged as a second implementation. The local release
branch is advanced only after the integrated checks above pass.
