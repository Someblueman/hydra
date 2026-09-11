# Native PTY acceptance

These C drivers replace the nine Python files present at `ecaee6b` (eight
drivers and their shared observer). They execute real native interfaces and
inspect emitted terminal bytes through an independent ANSI/UTF-8 cell observer.
They do not call the product terminal parser to decide whether a rendered frame
is correct.

Run from the repository root:

```sh
make test-workspace-pty test-statistics test-fleet-controls \
  test-plan-workspace test-attached-pty test-fleet-recovery
```

`BUILD_DIR` selects the existing native application build. The standalone termviz
package compiles `test_pty.c` with `pty_support.c`, `screen_support.c`, and
`fixture_support.c`; its `make test-pty` invokes `--standalone` to run the workspace,
shell and interruption scenarios without Hydra or JSON-C. Hydra planning and
receiver drivers use JSON-C, already required by the native fleet build.

## Preserved scenario mapping

| Former driver | Native driver and retained checks |
| --- | --- |
| `test_pty.py` | `test_pty.c`: tree collapse/expand, independent pane scroll, focus, exact mouse-divider cell, no redundant frames, three sizes and no overflow; real shell exit/status, 512-row history, streaming during focus changes, alternate-screen client, OSC isolation, child geometry, reaping, owned background-job termination, exec failure; Hydra adapter freshness and pane state. |
| `test_statistics_pty.py` | `test_statistics_pty.c`: local time/scope filters and coverage, evidence/graph drill-down, preserved workspace scroll, mouse selection, failed-refresh last-good data, refresh recovery; four metric pages with known denominators, p50 and freshness at three sizes; failed/empty/responded fleet counts, host filtering, selection scrolling and drill-down. |
| `test_fleet_controls.py` | `test_fleet_controls.c`: exact selected task/request/spec for approve/reject/resume; stale snapshot and replaced request during confirmation cause no mutation; lost cancel response records one request; observer restarts display requested/delivered/confirmed stages without replay; uncertain outcome refuses cancellation; lock and dirty bytes remain intact. |
| `test_plan_workspace.py` | `test_plan_workspace.c`: actual compiler admission, revision identity across layouts, changed objective invalidates approval, new revision compiles, queued old frames during resize do not overflow, malformed JSON is visibly invalid. |
| `test_attached_pty.py` | `test_attached_pty.c`: real identity-checked tmux attachment and stale-instance refusal; exact/reported confidence labels; slow observations do not block terminal input; forms do not block output; bracketed paste, Unicode editing and Ctrl-C routing; unsent drafts across modes, zoom, detach/reconnect and resize; two sessions with independent history, mouse focus and draft submission; compact terminal dimensions; client-only exit preserves shell PIDs and leaves no client. |
| `test_plan_launch.py` | `test_plan_launch.c`: full digest at three sizes, incorrect/stale approval produces no run, durable receipt while execution is active, duplicate refusal, UI temporary-file cleanup, detached completion and exact sealed artifact; escaped tab/newline checkout framing, historical navigation, live artifact corruption refusal, trusted provenance remains distinct from spoofing text in selected task output. |
| `test_workflow_controls.py` | `test_workflow_controls.c`: explicit approval/rejection/cancellation, wrong confirmation has no effect, durable decision is separate from resume, only approved resumed work executes, UI can exit during execution, terminal runs cannot resume/cancel, selection clears stale footer, unsent attached input and original panes survive. |
| `test_fleet_recovery.py` | `test_fleet_recovery.c` with `fleet_recovery_support.c`: two actual local receiver homes and controlled SSH loss, exact recorded-owner failure isolated to one task, independent receiver completion, result verification; observer exit/restart preserves task/run/attempt, complete ordered cursor stream, real 64-byte owner-log prefix plus nonempty resumed suffix equals full log, exact work artifact, no new acceptance/replay, saved ASCII announcements and owned-fixture quiescence. |

Every session checks the expected exit status and exact terminal flag, character
control and baud-rate restoration. The shared observer retains cursor addressing,
full-clear acknowledgement, SGR colors/reverse/bold, wide and combining characters,
incremental UTF-8 decoding, overflow accounting and HTML cell evidence.

Receiver cleanup uses the existing lock-based fixture quiescence helper. Tmux
wrappers use a fixture-owned socket; teardown does not enumerate or kill unrelated
user sessions. Failed receiver runs retain their fixture. Set
`HYDRA_TEST_KEEP_FIXTURE=1` to retain a successful receiver run, including commands,
observation pages, announcements, result and summary JSON.

Fixed asynchronous footer checks now use bounded waits for the same required
message, followed by the unchanged no-mutation assertion. These waits do not
relax approval, identity or result requirements.
