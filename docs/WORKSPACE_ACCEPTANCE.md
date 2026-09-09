# Native workspace and embedded-shell acceptance

Qualified locally on macOS on 7 September 2026, in the visualization worktree.
The C99 core and examples add no third-party dependencies. Hydra now opens its
native interactive workspace by default; `o` still opens the overview and `W`
returns to the workspace. The headless default and CLI/state contracts are unchanged.
The standalone demo embeds one real child shell. Hydra heads remain owned by tmux
and the shell CLI.

## Reproduce

```sh
make example-workspace
build/termviz-workspace
build/termviz-workspace --shell
make test-termviz test-tui test-tui-pty test-workspace-pty
make sanitize-workspace
make test-all
```

`make test-all` passed, including lint, shell, fleet, native C, PTY, visualization,
parity, installation and onboarding acceptance. The component suites and real PTY
suites passed. Native regression tests passed
79 assertions and existing PTY tests passed 110 assertions. The workspace suite
passed all four groups: standalone layout, embedded shell, process lifecycle, and
Hydra integration. `sanitize-workspace` repeated components and real PTY acceptance
with UndefinedBehaviorSanitizer. AddressSanitizer is not claimed: the repository's
macOS sanitizer configuration uses UBSan because ASan is unavailable in this local
environment. Other operating systems and terminal/font combinations remain untested.

| Requirement | Evidence |
| --- | --- |
| Incremental output | Presenter test: identical frame emits 0 bytes; one ASCII cell change emits 7 bytes. PTY test: idle demo emits no bytes; navigation, scrolling and dragging do not clear the screen. |
| Layout and input | Nested split/minimum-size component tests; real keyboard focus, mouse divider dragging, tree collapse/expand, independent pane scroll and resize at 40x10, 80x24 and 140x40. |
| Unicode and clipping | Wide and combining text, malformed UTF-8, borrowed clipped surfaces and wide-cell overwrite tests; independent PTY observer reports no terminal overflow. |
| Shell workflow | Real `/bin/sh -i` executes styled output and a failing command, whose status is observed as 1. 700 output lines fill the bounded 512-row history; focus changes during a 12-step stream. |
| Terminal modes and resize | Separate C child uses alternate screen, cursor addressing and RGB styles. Its ioctl-reported dimensions match the pane after each tested resize; exit restores primary screen. |
| Host escape isolation | Child OSC clipboard and DCS payloads are absent from raw outer-terminal output; safe text remains visible. Component parser is exercised at every split of a mixed stream and with 50000 deterministic hostile bytes. |
| Lifecycle | Normal child exit status, failed exec status 127, interrupted demo and owned shell/job cleanup; saved outer termios and file flags are restored exactly. |
| Hydra integration | Real adapter fixtures populate the default workspace; selection changes details; independent scrolling, drag and resize work. Existing native/PTY tests still exercise overview and other views. |
| Standalone extraction | Copied only termviz, example sources, component tests and licenses into an empty temporary directory. Strict C99 compiled the workspace and all six component executables, and all component tests passed there. |

## Standalone extraction recipe

Copy `src/termviz`, `examples/workspace*.c`, `examples/workspace_demo.h`,
`tests/c/test_termviz*.c` and the root `LICENSE`, preserving directory layout.
Retain `src/termviz/UNICODE-LICENSE.txt`. In that directory:

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -Isrc -Isrc/termviz \
  examples/workspace.c examples/workspace_view.c examples/workspace_input.c \
  src/termviz/*.c -o workspace
for source in tests/c/test_termviz*.c; do
  cc -std=c99 -Wall -Wextra -Werror -pedantic -Isrc -Isrc/termviz \
    "$source" src/termviz/*.c -o component-test || exit 1
  ./component-test || exit 1
done
./workspace --shell
```

No Hydra source, configuration, tmux, JSON-C, package manager or generation step
is present in this extracted build. This proves an extraction boundary, not a
published API or standalone release.

## Rendered inspection and limits

`test-workspace-pty` saves nine HTML cell reconstructions under
`build/workspace-evidence/`: workspace, shell and Hydra at each tested size. These
are reconstructed from actual PTY output by a separate Python observer, not mock
screens or output from the C terminal model. Browser inspection covered the 40x10
workspace, 140x40 embedded shell and 80x24 Hydra workspace. Borders, focus state,
clipping, selected head details, shell styles and compact quit hint were visible.
At narrow sizes, minimum-size collapse hides a subtree; focus cycling reveals it.
Long text is clipped, and pane scroll positions survive resizing.

This qualification does not establish full xterm or agent-CLI compatibility.
Unicode handling uses bounded cell-width rules rather than general grapheme
shaping. History is physical rows without reflow. The process adapter owns its
child and ordinary jobs in its session, not deliberately detached daemons. Terminal
support and limits are specified in [TERMINAL.md](../src/termviz/TERMINAL.md).
The later [attached-terminal qualification](ATTACHED_TERMINALS.md) adds two visible
tmux clients, independent scrollback and preserved drafts. Real agent-response
qualification and Linux evidence are in [the later acceptance record](WORKSPACE_REAL_ACCEPTANCE.md).
Standalone publication requires a separate release decision.

## Integrated workspace checks, 7 September 2026

The current local checks extend the original standalone-shell qualification:
`make test-all` passed on the final integrated code, including installation and
onboarding; its local log is `build/workspace-final-test-all.log`.

| Boundary | Evidence |
| --- | --- |
| Planning and approval | `test_plan_workspace.py` and `test_plan_launch.py`: real compiler, changed revision, invalid proposal, exact-digest refusal/approval, durable launch receipt, UI exit and duplicate-launch refusal. |
| Intervention and results | `test_workflow_controls.py`: real approval wait, separate decision/resume, rejection and cancellation; selected-step output, sealed result verification and tamper refusal. |
| Navigation | The real plan-launch test groups a recorded run beneath its head, retains collapse/expand across refresh, and opens that run's evidence. Branch references are historical associations, not instance ownership. |
| Multiple clients | `test_attached_pty.py`: two real tmux sessions visible together, separate unsent drafts across A/B/C/D, mouse input to the selected client, independent scrollback, three sizes and client-only reconnect/exit. |
| Observation limits | Stale head snapshots, unknown agent attention and independent transport disconnection remain visible. Synthetic snapshot cases verify exact versus reported exit/failure labels; they do not claim the attached shell exited. Fleet fixtures distinguish failed/unknown from successful/empty host responses. |
| Local extraction | `make test-termviz-export` builds the library, examples and component/PTY tests outside Hydra. No Hydra domain code or runtime dependency enters the exported library. |

Actual PTY cell captures are generated in `build/plan-launch-evidence/`
(approval, navigation and verification), `build/workspace-control-evidence/`
(running, waiting, failed and cancelled), and `build/attached-evidence/`
(one/two clients), including 40x10, 80x24 and 140x40 cases. Browser inspection
is recorded separately from assertions; captures are terminal output, not mockups.
Focused attached-session and plan/control suites passed under local UBSan.
A separate UBSan stress probe also exercised 512 heads sharing run associations;
the navigation capacity limit remained explicit and the probe completed without
sanitizer errors.

The earlier Codex access-token refresh blocker was resolved for the qualification
by using a fresh, isolated app server with the refreshed native login. The
[8 September real-workflow record](WORKSPACE_REAL_ACCEPTANCE.md) supersedes that
blocked result and records successful model/tool interaction, three stages of
execution and recovery, final artifact verification and final-code checks.
Linux standalone component/PTY and ASan/UBSan qualification also passed there.
