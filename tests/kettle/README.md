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

`BUILD_DIR` selects the existing native application build. The standalone kettle
package compiles `test_pty.c` and `test_program_pty.c` with `pty_support.c`,
`screen_support.c`, and `fixture_support.c`; its `make test-pty` runs the runtime
scenarios and invokes `test-pty --standalone` to run the workspace, shell and
interruption scenarios without Hydra or JSON-C. Hydra planning and
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
| (new) | `test_program_pty.c` with `program_fixture.c` (`make test-program-pty`, part of `make test-workspace-pty`): the kettle runtime on a real terminal. The screen is entered and cleared once in the first frame; all output is whole synchronized updates; an idle program writes nothing; a forced repaint and SIGWINCH at three sizes rewrite every cell without erasing the display and reach the last column and row; paste, focus and cursor modes; SIGTERM/SIGINT/SIGHUP/SIGQUIT exit 128+signal; SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT, `abort()` and `exit()` keep their status; in every case the last bytes written undo the terminal modes and the settings are restored exactly; `kt_cmd_exec` runs its child on a cooked terminal; suspend, SIGTSTP and SIGCONT re-enter and repaint; an update that waits for an answer runs the loop itself (question drawn in the frame, silent when idle, resize and paste pass through, a signal ends it restored); `kt_program_release` lends a cooked terminal and `kt_program_reclaim` re-enters and repaints, also when a quit signal arrived meanwhile. |
| (new) | `test_interactive_loop.c` (part of `make test-attached-pty`): what the control centre's loop guarantees, with a real attached tmux session. An idle control centre writes nothing across three refreshes and sleeps between them; 119 keys in one write are one frame; an open, idle prompt writes nothing and opens and closes without erasing the display, as do a theme change, resizes and workspace layout switches; a key typed to an attached agent is echoed with a median under 15 ms over 40 keys (the measured figures are printed); 120 keys typed to the agent in one write cost far fewer frames; the display is erased exactly once and all output is whole synchronized updates. |
| (new) | `test_attach_cycles.c`: eight attach, type, leave (`Ctrl-]`, `Ctrl-\ Tab`), Tab re-entry, close (`Ctrl-\ x`), layout switch and resize cycles at 140x40, 100x30, 80x24 and 60x20; leave/close hints visible, Esc delivered to the agent, one view per agent, a forced full repaint equals the incremental screen, no dotted fill, owner shell PID and all typed work survive; a disconnected client releases input; another smaller client's size is named; `Ctrl-B` reaches the agent's tmux as its prefix; a pending chord is shown, an unbound one named and swallowed, both reserved keys typed literally; a keys file restoring the `Ctrl-B` prefix changes chords, hints and help; a malformed or conflicting keys file falls back with a notice naming file, line and reason. HTML and raw PTY evidence in `build/attach-cycles-evidence`. |
| (new) | `test_output_fidelity.c`: recorded Claude Code and Codex output printed in a head, read back in the Details transcript and the attached pane at 140x40 and 80x24 with resize and tmux history: verbatim Unicode, aligned wide cells, provider and themed diff colors, wrapped long lines with markers, no escape residue or `?` runs, key hints labelled. Evidence in `build/output-fidelity-evidence`. |
| (new) | `test_responsive_reads.c` (part of `make test-tui-pty`): no key waits on a subprocess. Against a fixture CLI and tmux whose read-only answers take seconds, opening Workflows and Statistics, `r`, `g`, `p` and the agent-history chord are each followed by a key that must act within 0.7 s while the read still runs; the views say they are loading and show the answer when it arrives; twelve refresh keys in a row run one statistics read at a time (the fixture CLI's own start/end log is the evidence). |
| `test_fleet_recovery.py` | `test_fleet_recovery.c` with `fleet_recovery_support.c`: two actual local receiver homes and controlled SSH loss, exact recorded-owner failure isolated to one task, independent receiver completion, result verification; observer exit/restart preserves task/run/attempt, complete ordered cursor stream, real 64-byte owner-log prefix plus nonempty resumed suffix equals full log, exact work artifact, no new acceptance/replay, saved ASCII announcements and owned-fixture quiescence. |

The control centre learns of a new size from SIGWINCH, so its sessions are opened with `kt_open_terminal`: the PTY is the child's controlling terminal, as under a terminal emulator, and `kt_resize` raises the signal.

Every session checks the expected exit status and exact terminal flag, character
control and baud-rate restoration. The shared observer retains cursor addressing,
resize acknowledgement, SGR colors/reverse/bold, wide and combining characters,
incremental UTF-8 decoding, overflow accounting and HTML cell evidence. A resize
is acknowledged by a display erase (`hydra-tui`) or by one synchronized update
(DEC mode 2026) that paints every cell of the new grid (the kettle runtime, which
never erases for a repaint); output drawn for the old size is discarded either
way. Sessions opened with `kt_open_terminal` make the PTY the child's controlling
terminal, as a terminal emulator does, so a resize raises SIGWINCH.

Receiver cleanup uses the existing lock-based fixture quiescence helper. Tmux
wrappers use a fixture-owned socket; teardown does not enumerate or kill unrelated
user sessions. Every `hydra_fixture.c` driver starts a guardian process in its own
session: when the driver exits for any reason (success, a failed check, a signal,
even `SIGKILL`) the guardian kills the private tmux server and any process whose
command line names the fixture, then removes the fixture directory. A successful
driver also checks that no process still names its socket. `test_terminal_restore.c`
kills an interactive head's tmux session, then checks that opening it from Work
restarts the terminal in the same worktree with committed and uncommitted work
intact, and that a missing worktree is refused without recreating anything.
`HYDRA_TEST_KEEP_FIXTURE=1` keeps a `hydra_fixture.c` directory (never its tmux
server) for inspection. Failed receiver runs retain their fixture. Set
`HYDRA_TEST_KEEP_FIXTURE=1` to retain a successful receiver run, including commands,
observation pages, announcements, result and summary JSON.

Fixed asynchronous footer checks now use bounded waits for the same required
message, followed by the unchanged no-mutation assertion. These waits do not
relax approval, identity or result requirements.
