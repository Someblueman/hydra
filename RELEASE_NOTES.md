# Hydra v1.8.0 Release Notes

**Release date:** 2026-08-30

## Highlights

Hydra 1.8.0 adds optional native mission control over the lifecycle and coordination
contracts established in 1.6 and 1.7. The shell CLI remains the mutation authority,
and the maintained basic shell TUI remains the default through the first patch
release.

## Native mission control

- `hydra tui --native` provides head list/detail, coordination, and recovery views,
  keyboard navigation and search, selected-pane preview, and a fixed local-action
  palette.
- Declared outcome, observed status and confidence, liveness, stale state, and
  unavailable state remain visibly distinct. Event, signal, message, gate, claim,
  scope, queue, resource, diff, and approval counts come from inspectable records.
- Native mutations use argv-safe execution of the shell CLI. There is no command
  string interpolation, natural-language command parser, native state writer, or
  native notification delivery.
- Recovery findings cover malformed state, stale same-host locks, dead sessions,
  orphan worktrees, interrupted transitions, and teardown failures without applying
  recovery automatically.

## Terminal and fallback behavior

- `hydra tui` and `hydra tui --basic` use the existing shell TUI. `--native` is an
  explicit opt-in; `--capabilities` explains availability and dispatch policy.
- The native UI restores terminal state on normal exit and termination signals,
  clips narrow/resize layouts, works without color, and fails clearly for `TERM=dumb`
  or non-TTY input/output.
- Pane and fixture data are untrusted. Control and invalid bytes are replaced before
  rendering; paste, escape sequences, preview bytes, and record counts are bounded.
- The production refresh path uses bounded polling. The tmux control-mode prototype
  did not yet prove reconnect and flow-control reliability, so it is not enabled.

## Build and installation

```sh
make build-tui test-tui
make sanitize

HYDRA_INSTALL_TUI=required HYDRA_BUILD_TUI=1 \
  PREFIX=$HOME/.local ./install.sh
```

Offline TUI artifacts carry checksum, OS/architecture, dependency, and source
metadata and receive exact version/protocol verification before atomic replacement.
Use `HYDRA_INSTALL_TUI=never` for a shell-only installation.

## Upgrade from 1.7.x

```sh
git pull
PREFIX=$HOME/.local ./install.sh
hydra version                 # should show 1.8.0
hydra tui --capabilities
hydra tui --basic             # unchanged fallback
```

No state migration is required from 1.7. The native TUI consumes the existing shell
and state-v2 contracts.

## Validation

```sh
git diff --check
make lint
make test-all
make sanitize
```

The deterministic terminal lane uses `hydra tui --headless-fixture` at explicit
sizes and frame counts. Hosted exact-candidate CI and manual terminal/accessibility
review remain release-publication evidence, separate from local implementation.

## Deferred

Native-by-default dispatch, tmux control-mode production observation, mouse support,
animations, themes, workflow editing, fleet view, and natural-language commands
remain deferred.
