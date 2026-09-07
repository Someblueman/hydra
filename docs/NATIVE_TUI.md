# Native mission control

Hydra includes a C99 terminal UI over the same shell-owned lifecycle and coordination
records as the basic POSIX-shell TUI. The native UI reads and renders; the shell CLI
remains the only mutation and policy authority.

## Dispatch and fallback

```sh
hydra tui                 # native-first; visibly falls back to the basic TUI
hydra tui --basic         # explicit basic recovery path
hydra tui --capabilities  # availability and observation diagnostics
hydra tui --view heads    # original head table
hydra tui --view workflows # recorded workflow dependency graph
hydra tui --view statistics # recorded workflow statistics (D)
hydra tui --ascii         # ASCII graphics on terminals/fonts without Unicode
hydra tui --theme dark    # fixed dark palette; terminal and light also supported
```

Plain `hydra tui` falls back visibly to the basic TUI when the native binary is
missing, the terminal is unsuitable, or the native process reports a recoverable
failure. Invalid native arguments and user interrupt signals remain fail-closed.
`HYDRA_TUI_BIN` selects a qualified native binary during testing or custom
installation.

The normal source installer builds and installs the native TUI when `make` and a C99
compiler are available. `HYDRA_INSTALL_TUI=never` keeps a compiler-free shell-only
installation, where plain `hydra tui` visibly enters the basic fallback.

The production refresh path requests a shell snapshot every two seconds with a
two-second subprocess timeout. Periodic observations run asynchronously, so slow
collection does not block attached terminal input. An optional selected-preview
`tmux capture-pane` is bounded to one second and 4095 bytes. The earlier tmux
control-mode prototype met its latency experiment, but did not establish reconnect,
flow-control, and terminal-matrix reliability. Control mode therefore remains off
instead of becoming an unqualified event authority.

## Views and confidence

The interactive default is the workspace: a project/head/run tree, selected-head
details, and activity/recovery panes. `Tab` cycles focus; `j/k` navigate the tree
or scroll the focused content pane independently; `h/l` collapse/expand tree
branches. Drag either divider to resize. When minimum sizes cannot fit, focus
cycling reveals hidden panes. `W` returns to this workspace; `--view workspace`
selects it explicitly. Rendering retains the previous frame and emits changed
regions; resize, palette changes and delegated terminal ownership invalidate it.

`o` opens the overview dashboard. Wide terminals show summary cards, a selectable
head table, selected-head details, queue history, session-state distribution, and
changed-file bars. At 80 columns the overview stacks the table and chart. `w` opens
workflows and `H` opens remote hosts. The deterministic headless interface retains
its original default head table. The standalone [termviz workspace demo](../src/termviz/README.md)
adds an embedded real shell. Hydra's workspace instead embeds attachment clients
to existing tmux heads; see [attached terminals](ATTACHED_TERMINALS.md).

`D` opens the dedicated statistics view and returns to the previous view without
changing workspace selection, pane focus or scroll. It shows a filtered run cohort,
recorded outcome distribution, retries, latest-attempt timing and a seven-day run
creation chart. `Enter` inspects contributing steps; `g` opens the selected run's
existing graph and `Esc` returns to the same statistics filters. Fleet mode shows
host response coverage and known head counts. See [statistics definitions and
limits](STATISTICS.md). This view does not yet include an embedded agent pane.

The workspace's selected-work pane emphasizes agent, group, host, project and
changes. `a` opens an identity-checked client to the selected head inside the
workspace. `Ctrl-B Tab` transfers input back to Hydra; `Ctrl-B D` opens statistics.
The existing tmux session retains the agent process and unsent input.

Queue history holds up to 120 refresh observations, not repaint frames. Its
horizontal domain is **samples**, since collection intervals can vary. Only heads
with a recorded identity contribute to this known-data count. Failed refreshes
create gaps; they do not fabricate zero entries. The last good snapshot remains
visible with a stale indication and its age. Session state and file-change bars
are snapshot distributions, not throughput or completion estimates.

Workflow graphs read recorded local runs through `hydra workflow tui-data`.
Arrows go from prerequisite to dependent. `j/k` selects a node, `[/]` switches
runs, and `h/l/J/K` pans. `Enter` recenters on selection. Mouse clicks select
visible nodes; a narrow terminal shows the selected step and its dependencies.
The model rejects duplicate IDs, missing dependencies, and cycles. Its bounds are
32 runs, 512 total steps, 128 steps and 512 edges per graph. Limit or missing-record
warnings remain visible; this view never executes or repairs a workflow.

`hydra fleet tui` supplies remote observations. The host view includes successful
empty hosts and failed hosts, distinguishes a list response from process liveness,
and exposes the failure code. Selecting a host and pressing `Enter` filters the
head list. CPU, memory, and load are explicitly unavailable: the current remote
protocol does not collect them. Remote workflow graphs are likewise not present;
the graph view covers Hydra's existing local workflow records.

The head list puts branch identity first, followed by session status. Wider
terminals also show the agent and reported outcome. Session liveness never implies
agent completion; stale and unavailable sessions remain explicit. Fleet lists use
host-qualified names and label **desired state**, without claiming live activity.

Details focus on agent, session, reported outcome, changed files, messages, and
gates. `p` opens terminal output in the detail view. `d` opens diagnostics with
raw state, confidence, adapter information, source paths, and instance identifiers.
These fields remain inspectable without crowding the everyday views.

Coordination focuses on the selected head's changes, gates, scopes, queue, and
resources. The action palette opens the corresponding shell commands. Recovery
lists findings; use `j/k` to select one and `d` to inspect its source and suggested
command. Recovery never runs automatically.

The title bar and view navigation sit above a bordered content panel. Lists use
fixed, padded columns with visible separators and a selected-head summary below
the table on larger terminals; narrower terminals show fewer
columns. Long values are clipped within their own cells and remain available in
details. Selected rows have a full-width highlight; unavailable local sessions
use a warning color. Details separate identity, work summary, and terminal output
into named areas.

Rendering respects terminal height and reserves the footer for actions. Long head
and recovery lists follow the selection. Color highlights the title and selected
row; selection markers and labels remain usable with `NO_COLOR` or `--no-color`.

## Keyboard map

| Key | Action |
| --- | --- |
| `j` / `k`, arrows | Move through heads, workflow nodes, hosts, or recovery findings |
| `Enter` | Open selected-head detail |
| `v` | Cycle heads, detail, coordination, recovery, overview, workflows, hosts, workspace, and statistics |
| `D` | Toggle statistics; retain filters and previous workspace context |
| `a` in workspace | Attach the selected head in the conversation pane |
| `Ctrl-B` in conversation | Prefix for Hydra focus, view, client and exit controls |
| `S` in workspace / `Ctrl-B S` in conversation | Toggle two visible attached agents in the current layout |
| `W` / `Tab` | Open workspace / cycle workspace pane focus |
| `h` / `l` in workspace | Collapse / expand navigation tree |
| `o` / `w` / `H` | Open overview / workflow graph / hosts |
| `[` / `]` | Previous / next recorded workflow run |
| `h` / `l` / `J` / `K` | Pan the workflow graph left / right / down / up |
| `/` | Search branch, session, group, profile, remote host, or project |
| `:` | Search explicit local actions |
| `p` | Open details and toggle sanitized terminal output |
| `d` | Toggle diagnostics for the selected head or recovery finding |
| `Esc` | Return to heads and clear search, help, and diagnostics |
| `Space` / `A` | Toggle selection / select all visible heads |
| `x` | Kill selected heads through the shell CLI; skip the current tmux session |
| `G` | Assign selected heads to a group through the shell CLI |
| `?` | Toggle in-product keyboard and action help |
| `t` | Cycle terminal, dark, and light palettes |
| `q` | Exit and restore terminal state |

The palette is a fixed action list, not a natural-language command interpreter.
Spawn prompts separately for branch, profile, template, and layout, then executes
those fields as an argument vector through the shell CLI. Switch, kill, regenerate,
and dashboard use the same argv boundary. Commands are never assembled into a shell
string. Bulk kill performs a bounded tmux lookup for current-session safety, then
runs the public, confirming `hydra kill <branch>` command for each remaining head.

## Mouse navigation

Mouse navigation is enabled while the native UI owns the terminal. Click a head
or recovery row to select it; the wheel over list rows moves selection exactly as
`j/k` does, including filtered and scrolled lists. On terminals at least 70 columns
wide, click a view tab to switch views. Enter opens the selected head; `d` inspects
a recovery finding. Clicking a row never attaches, interrupts, or mutates a head.

In the workspace, click a pane to focus it and drag a divider to resize; the
wheel navigates or scrolls that pane. Other views keep their existing behavior:
headers, borders, empty space, help, and diagnostics do not select rows. Release,
drag, modified clicks, and malformed reports are ignored outside workspace dividers. Hit targets come from
the last painted frame; a resize invalidates them until the next render. At narrow
widths or during search, use `v` for view navigation. Mouse reporting is disabled
for prompts, delegated commands, and exit, including the basic fallback after a
native crash. Terminal-native text selection may require holding Shift.

Dragging workflow graphs and historical sparklines remains outstanding. No new activity or progress metrics are inferred from session status
or fleet desired state.

## Themes

The default `terminal` palette keeps the terminal's foreground/background and uses
reverse video for the title and selected row. `dark` uses a black background with
light text and cyan selection; `light` uses a light background with dark text and
blue selection. The explicit dark/light palettes use fixed 256-color entries to avoid pastel
or remapped ANSI base colors weakening contrast. Use `terminal` on terminals
without 256-color support. Text markers, labels, and fixed column spacing carry
the same meaning in every palette.

Press `t` to cycle palettes without losing selection, search, or the current view.
Set `HYDRA_TUI_THEME=terminal|dark|light` to choose a startup palette for
`hydra tui` or `hydra fleet tui`. When invoking the native `hydra-tui` executable
directly, `--theme terminal|dark|light` selects its startup palette.
An explicit option overrides the environment. Theme changes apply to the current
process; there is no saved layout/config file. Unknown names fail with a usage
error before raw mode. `NO_COLOR` and `--no-color` suppress every palette, including
when `t` is pressed. Headless fixture output remains plain and deterministic.

## Terminal safety and accessibility

- Normal exit and `SIGINT`, `SIGTERM`, or `SIGHUP` restore canonical input, cursor,
  and rendition state.
- `TERM=dumb`, non-TTY input/output, and terminals narrower than 40 by 10 fail with
  an explicit basic-TUI recovery.
- The native renderer does not depend on color, so `NO_COLOR`, monochrome, and
  low-color terminals preserve all status meaning.
- Pane and state bytes are untrusted. The termviz views decode bounded UTF-8 with
  explicit cell widths; older line-based views retain ASCII replacement. Controls
  and malformed text are replaced before rendering; escape sequences cannot inject commands or terminal
  controls. Bracketed paste and unknown escape sequences are bounded and ignored; SGR
  mouse reports accept only bounded numeric coordinates and navigation buttons.
- The layout clips to the current terminal size and the recovery view remains useful
  at narrow widths. All actions are keyboard accessible.

## Deterministic headless fixtures

CI and release qualification do not need platform-specific `script(1)` behavior:

```sh
hydra tui --headless-fixture tests/fixtures/tui/native-v2.tsv \
  --size 80x24 --frames 2 --view recovery
```

The fixture begins with `HYDRA_TUI<TAB>2`, followed by bounded `H` head records and
`R` recovery records. Malformed handshakes or records fail closed. This format is an
internal 2.0 native/basic parity boundary, not the general automation API; scripts
should use the documented CLI JSON envelopes.

`make bench-tui` creates 5, 20, and 100 live sessions on one isolated tmux socket,
measures the shell adapter and ten headless frames, and then measures native startup
and a 2.2-second interactive PTY window over the same live snapshot. It enforces
budgets of 1000 ms adapter refresh, 100 ms per ten renders, 3000 ms interactive
startup, and 40% native-plus-adapter CPU during the interactive window. The CPU
ceiling accommodates the bounded refresh cost at the 100-head stress point; measured
values remain part of the emitted JSON evidence.

The final 2026-08-31 macOS arm64 local run measured adapter/render/startup at
151/14/197 ms for 5 heads, 279/21/1174 ms for 20, and 701/43/2373 ms for 100.
Interactive CPU was 13%, 15%, and 28%. Selected-pane capture remains disabled unless the user opens the
preview, so those figures qualify the normal control-surface refresh path rather
than continuous pane streaming.

## Accessibility review

The 2026-08-31 local review covered keyboard-only navigation and search in a real
pseudo-terminal, explicit and environment-driven no-color modes, 54-column and
minimum 40-by-10 rendering, and the visible `LIVE`, `STALE`, `UNAVAILABLE`, declared,
observed, confidence, and recovery language. The review found and fixed a narrow-list
defect that clipped branch identity: narrow rows now retain both status and branch.
The PTY and deterministic renderer assertions preserve this review mechanically.

Hosted Linux and macOS execution remains a release-candidate evidence step; local
success alone does not satisfy that platform-matrix gate.

## Build, install, and qualification

```sh
make build-tui test-tui
make sanitize-tui
make package-tui

HYDRA_INSTALL_TUI=required HYDRA_BUILD_TUI=1 \
  PREFIX=$HOME/.local ./install.sh
```

Offline `hydra-tui` artifacts use adjacent `.sha256`, `.platform`, `.dependencies`,
and `.source` metadata. Installation verifies checksum, host platform, protocol, and
exact Hydra/TUI release version before atomic replacement. Shell-only installation
remains available with `HYDRA_INSTALL_TUI=never`.

## Draft planning workspace

The workspace defaults to A (conversation). B opens the plan overview and toggles
back to A; C shows observed workflow dependencies. Each layout retains its own
focus, split proportions, zoom and pane scroll. Attached terminal clients and their
unsent input remain shared; use Ctrl-B A/B/C while input goes to an agent. D retains
its existing statistics filters and returns to the previous workspace layout.

P opens native draft/policy path fields. These dialogs keep terminal output and
observations running; pasted newlines cannot submit them. V compiles the loaded
revision through `hydra workflow plan compile`, then displays the complete public
preview and dependency projection. Compilation does not execute work. Changes to
either original file increment the displayed revision and clear the compiled
digest. Failed validation remains visible; missing or oversized projections are
reported unavailable. Draft/policy inputs are bounded at 256 KiB each and preview
text at 1 MiB. Temporary compilation snapshots are removed on normal UI exit.

E requires typing the exact 64-character digest shown for the current revision.
Submission rechecks the revision, draft and policy before sending an open compiled
snapshot to a detached owner. The existing workflow engine performs admission,
publishes the run and schedules execution. Closing the UI does not cancel it.
The workspace records one launch per project/digest under
`state/v2/projects/<project>/workflows/launches/<digest>`; `run-id` points to the
existing workflow run, `owner.log` contains diagnostics and `exit-code` records
owner completion. A reserved or uncertain launch is never automatically retried.
In C, `[` and `]` select recorded runs; j/k in the dependency pane selects a step.
The evidence pane follows that exact run/step, showing its attempt, exit code and
the last 8192 bytes of each output stream. Input requests include their message,
identity, binding, expiry and decision. For successful compiled plans, result
retrieval rechecks the sealed artifacts and displays deliverable hashes, paths and
check evidence. Recorded success alone never displays verified evidence. Failed
or incomplete reads retain an explicitly stale observation; changing selection
clears evidence from the previous run or step.
The fixed header's verification status comes from the result verifier. Output
text containing a success claim cannot set that status.

C also supports Y to approve a request, N to reject it, R to resume and X to cancel.
Each action names the selected run and requires typing the action; decisions also
require the request ID shown in evidence. The existing CLI checks request binding
and expiry. A recorded decision does not automatically resume work. Control owners
survive UI closure and record output in the run's `workspace-controls.log`, shown
in evidence. Terminal runs cannot be resumed or cancelled from the workspace.
These controls do not extend the planning schema: compiled plans still support
spawn/exec only, with zero retry and repair budgets. Existing workflow definitions
can contain approval waits. Revising execution scope needs fresh approval.

The local navigation tree groups recorded runs beneath matching head branches;
runs without a visible matching head remain under the current project. Enter on
a run opens its monitoring evidence. Head expansion and selected run survive
refresh and layout changes. Associations come from recorded graph arguments,
not current instance ownership. The read-only projection is bounded to 32 runs
and 512 branch references, matching the graph view's run limit.
Fleet workspace activity lists existing host responses and distinguishes failed
responses with unknown counts from successful empty responses. H opens the
selected head's host details without adding remote execution or telemetry.

Real agent-authored acceptance and final workspace visual review remain on
the roadmap. Real Codex model-response qualification remains blocked by the local
CLI login, despite verified composer, resizing and reconnect behavior.

`make test-plan-workspace` exercises actual compilation and revision invalidation
through a PTY at 40x10, 80x24 and 140x40, plus stale/wrong approval refusal,
detached execution, duplicate launch refusal, artifact-bound verification and
tamper refusal. Real workflow request tests cover explicit decisions, separate
resume, rejection and cancellation while an attached shell retains unsent input.
`make sanitize-plan-workspace` repeats it
with the repository sanitizer flags. Attached-session checks cover unsent input
across A/B/C/D without executing it during transitions.
