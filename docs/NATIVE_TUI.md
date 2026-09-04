# Native mission control

Hydra includes a C99 terminal UI over the same shell-owned lifecycle and coordination
records as the basic POSIX-shell TUI. The native UI reads and renders; the shell CLI
remains the only mutation and policy authority.

## Dispatch and fallback

```sh
hydra tui                 # native-first; visibly falls back to the basic TUI
hydra tui --basic         # explicit basic recovery path
hydra tui --capabilities  # availability and observation diagnostics
```

Plain `hydra tui` falls back visibly to the basic TUI when the native binary is
missing, the terminal is unsuitable, or the native process reports a recoverable
failure. Invalid native arguments and user interrupt signals remain fail-closed.
`HYDRA_TUI_BIN` selects a qualified native binary during testing or custom
installation.

The normal source installer builds and installs the native TUI when `make` and a C99
compiler are available. `HYDRA_INSTALL_TUI=never` keeps a compiler-free shell-only
installation, where plain `hydra tui` visibly enters the basic fallback.

The production refresh path uses one shell snapshot every two seconds with a
two-second subprocess timeout, plus an optional selected-preview `tmux capture-pane`
bounded to one second and 4095 bytes. The earlier tmux
control-mode prototype met its latency experiment, but did not establish reconnect,
flow-control, and terminal-matrix reliability. Control mode therefore remains off
instead of becoming an unqualified event authority.

## Views and confidence

The head list and detail view keep declared outcome, observed status, observation
confidence, desired state, and liveness separate. The renderer labels stale and
unavailable state explicitly and never turns pane activity or process existence
into an agent-completion claim.

The coordination view summarizes events, signals, messages, claims, scopes, queued
work, allocated resources, changed files, gates, and approvals. The action palette
opens the existing shell views for claims, collisions, scopes, queue, resources,
Git diff, and gates. Every detail names its inspectable state record.

The recovery board reports evidence-backed dead sessions, malformed state records,
stale same-host locks, orphan worktrees, interrupted lifecycle state, and teardown
failures. Suggestions are inspect-first commands; recovery does not run
automatically.

Adapter capability and confidence come from the selected recorded profile and link
to `hydra capabilities --json` or the custom profile directory. The configured
notification count and source file are shown while delivery remains delegated to
the shell lifecycle path.

## Keyboard map

| Key | Action |
| --- | --- |
| `j` / `k`, arrows | Move through heads |
| `Enter` | Open selected-head detail |
| `v` | Cycle heads, detail, coordination, and recovery views |
| `/` | Search branch, session, group, or profile |
| `:` | Search explicit local actions |
| `p` | Toggle the sanitized selected-pane preview |
| `Space` / `A` | Toggle selection / select all visible heads |
| `x` | Kill selected heads through the shell CLI; skip the current tmux session |
| `G` | Assign selected heads to a group through the shell CLI |
| `?` | Toggle in-product keyboard and action help |
| `q` | Exit and restore terminal state |

The palette is a fixed action list, not a natural-language command interpreter.
Spawn prompts separately for branch, profile, template, and layout, then executes
those fields as an argument vector through the shell CLI. Switch, kill, regenerate,
and dashboard use the same argv boundary. Commands are never assembled into a shell
string. Bulk kill performs a bounded tmux lookup for current-session safety, then
runs the public, confirming `hydra kill <branch>` command for each remaining head.

## Terminal safety and accessibility

- Normal exit and `SIGINT`, `SIGTERM`, or `SIGHUP` restore canonical input, cursor,
  and rendition state.
- `TERM=dumb`, non-TTY input/output, and terminals narrower than 40 by 10 fail with
  an explicit basic-TUI recovery.
- The native renderer does not depend on color, so `NO_COLOR`, monochrome, and
  low-color terminals preserve all status meaning.
- Pane and state bytes are untrusted. Control bytes and non-ASCII byte sequences are
  replaced before rendering; escape sequences cannot inject commands or terminal
  controls. Bracketed paste and mouse/unknown escape sequences are bounded and
  ignored.
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
