# Native TUI enhancement qualification — 5 September 2026

Local development evidence, not a release or hosted-platform qualification.
Baseline: `codex/release-next` at `8e893cd`; only `.gmcs/` was untracked.
The separate `3afc` worktree was on `main` at `d3090e4` and was not changed.

## Mouse navigation slice

`make test-tui test-tui-pty sanitize-tui` passed on macOS arm64: 75
renderer/adapter checks, 73 PTY checks, and supported UBSan rendering. `make lint`
and `git diff --check` passed. PTY checks exercise row/tab clicks, wheel/key parity,
filtered and short scrolled lists, resize, recovery diagnostics, invalid/overlong
SGR and legacy mouse reports, prompt handoff, signals, quit, and crash fallback.
Existing long-column and minimum-size rendering assertions still pass.

The refreshed quick-tour recording uses a new disposable local project, real Codex
plan output, and a disposable head on the qualified remote installation described
in FLEET_ACCEPTANCE.md. Both local heads and the remote head were removed through
Hydra, and the script checked local worktree cleanup. The recording retained the
private SSH alias and neutral prompt/status labels. Inspection covered the head
list, selected summary, actual terminal output, remote desired-state list, attached
Linux terminal, disconnect, and return to mission control. No real SSH address was
visible in inspected attach/disconnect frames. The first render was monochrome
because the recording inherited NO_COLOR; selection remained readable.

Mouse reports only navigate. Narrow/search views retain keyboard view switching;
row click does not activate lifecycle actions. Layout dragging, workflow graphs,
themes, and historical sparklines are outside this slice.

## Theme slice and combined qualification

The terminal/default, dark, and light palettes preserve textual status, selection,
search, and view behavior. The focused suite now passes 79 deterministic checks
and 87 PTY checks, including a second 87-check run against the UBSan build.
Final lint and whitespace checks passed. Tests include startup selection, command-line precedence,
invalid names, background repaint, keyboard parity, and NO_COLOR during switching.
`make test-all` passed during integration, including shell, fleet, native, parity,
install, and onboarding acceptance. Final focused checks were rerun after the
visual correction below; hosted Linux/macOS qualification remains a release step.

A separate live browser terminal was opened against two real disposable local
shell heads. Clicking the second row selected it; clicking Details opened that
head; Escape and wheel-up returned selection to the first row. Theme switching
preserved that state. Visual inspection found bold black becoming gray in the
terminal palette, weakening dark selection contrast. Removing bold from that
explicit black/cyan pairing fixed the observed issue, confirmed in a new process.
The terminal, dark, and light layouts were inspected directly. The recording
then exposed low contrast from pastel ANSI base colors in the light theme;
explicit dark/light palettes now use fixed 256-color entries instead. The review heads
were removed via Hydra, the single remaining project worktree was checked, and
the owned terminal server and tmux socket were stopped.

The final quick-tour explicitly enables color and cycles all three palettes before
showing fresh real Codex output and a fresh remote head. This opt-in applies only
to the recording script. Production NO_COLOR behavior remains intact. Final palette frames were inspected
after the 256-color correction; dark/light selections remained readable under the
recording terminal's pastel base palette. Final remote attach/disconnect and
return frames retained neutral labels with no private SSH address visible.

## Remaining boundaries

No draggable split, workflow graph, or sparkline is implemented. The current TUI
snapshot contains aggregate counters, not a timestamped activity series; deriving
sparklines from it would imply information it does not contain. A workflow graph
needs the recorded workflow graph and step states plus a useful inspection action.
These remain roadmap work, as does any saved layout preference. Fleet desired
state is still distinct from observed activity and task completion. Versions,
released history, hosted checks, tags, pushes, and publication are unchanged.
