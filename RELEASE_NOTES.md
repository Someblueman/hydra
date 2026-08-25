# Hydra v1.5.1 Release Notes

**Release date:** 2026-08-25

## Highlights

Hydra 1.5.1 hardens the shipped POSIX shell CLI so it is a trustworthy baseline:
locking and atomic file replace, kill/regenerate/queue/JSON correctness, explicit
tmux pane targets, and a documented 1.5 public contract. No state migration is
required.

## Correctness

- Map writers share one lock and no longer write if acquisition fails
- Temp files for map/tags rewrites stay on the same filesystem as the destination
- `hydra status` reads all seven map fields and validates duration timestamps
- `hydra kill` checks dirty worktrees before killing tmux or dropping the mapping
- Message inboxes are deleted on kill and dead-mapping cleanup
- Doctor/cleanup stale locks match the `mkdir` `*.lock` directory format
- `hydra regenerate` keeps group, dependency, PR, and timestamp metadata
- Spawn queue processes high priority first, FIFO within one priority
- JSON strings escape every C0 control character

## tmux and TUI

- Startup and agent launch target `session:0.0`
- `hydra broadcast` prefers a non-agent shell pane; refuses agent-only sessions unless `--force` or `--pane`
- TUI rows are eight tab-separated fields end to end
- List/status/TUI refresh batch tmux session and pane observations (`window_activity` first)

## Docs and tooling

- [docs/CONTRACTS.md](docs/CONTRACTS.md) records the 1.5 public surface
- README states supported platforms
- `make bench` records shell timings (measurements only)
- Completions include `group`, `send`, `recv`, `tail`, `broadcast`, `wait-idle`, `queue`

## Upgrade

```sh
sudo ./install.sh   # or: sudo make install
hydra version       # should show 1.5.1
```

No migration steps required for `~/.hydra` state files. Non-root `PREFIX`
install remains a 1.5.2 item.

## Test Coverage

Run `make lint` and `make test`. Passing runs may still print `Error: ...`
lines from expected error-path assertions.
