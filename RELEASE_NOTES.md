# Hydra v1.5.0 Release Notes

**Release date:** 2026-08-24

## Highlights

Hydra 1.5.0 is a major usability and maintainability release: a refreshed TUI, comprehensive bugfixes from the 1.4.3 audit, and a full internal refactor that splits the largest source files into focused modules.

## TUI Refresh

- Two-panel layout with detail sidebar on terminals ≥100 columns wide
- **Enter** now switches sessions (`s` still works)
- **A** select all, **D** open dashboard, **f** preview follow mode
- Interactive spawn wizard with AI tool picker
- Search across branch, session, group, and AI tool
- Configurable refresh via `HYDRA_TUI_REFRESH_MS`

## Bugfixes

- Doctor/cleanup/regenerate correctly find `../hydra-<branch>` worktrees (including slash branches)
- Spawn rollback on invalid AI tool; queue fixes for mixed agents and failed retries
- Session limits count live tmux sessions only
- TUI bulk group assignment works via `set_group`

## Architecture

- `bin/hydra` is now a thin dispatcher
- Commands live in `lib/cmd_*.sh`
- TUI split into `lib/tui_*.sh`
- State cache in `lib/state_cache.sh`
- Shared maintenance checks in `lib/maintenance.sh`

## Upgrade

```sh
sudo ./install.sh   # or: sudo make install
hydra version       # should show 1.5.0
```

No migration steps required for `~/.hydra` state files.

## Test Coverage

Run `make test` — 25+ test suites including expanded paths and TUI tests.
