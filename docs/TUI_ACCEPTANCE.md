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
