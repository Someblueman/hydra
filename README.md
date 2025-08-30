<div align="center">
    <a href="https://github.com/Someblueman/hydra">
        <img width="300" height="200" src="assets/hydra.png" alt="Hydra Logo">
    </a>
    <br>
    <div style="display: flex;">
        <a href="https://github.com/Someblueman/hydra/actions?query=workflow%3Aci">
            <img src="https://github.com/Someblueman/hydra/workflows/CI/badge.svg" alt="CI Status">
        </a>
        <a href="https://github.com/Someblueman/hydra/releases">
            <img src="https://img.shields.io/github/release/Someblueman/hydra.svg" alt="Latest Release">
        </a>
        <a href="https://github.com/Someblueman/hydra/stargazers">
            <img src="https://img.shields.io/github/stars/Someblueman/hydra.svg" alt="GitHub Stars">
        </a>
        <a href="https://github.com/Someblueman/hydra/blob/main/LICENSE">
            <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT">
        </a>
        <a>
            <img src="https://img.shields.io/badge/POSIX-compliant-brightgreen.svg" alt="POSIX Compliant">
        </a>
    </div>
    <h1>Hydra</h1>
    <p>
        <b>POSIX tmux + git worktree orchestrator for parallel “heads”</b>
    </p>
    <p>
        One tmux session + worktree per branch. Fast switches, layouts, YAML, and a multi-session dashboard; optional GitHub issues and AI agents.
    </p>
</div>

## Quick Start

- Requirements: `git`, `tmux` (≥ 3.0). Optional: `fzf`, GitHub CLI, an AI CLI (`claude`, `aider`, `gemini`, etc.).
- Install:
  - `git clone https://github.com/Someblueman/hydra && cd hydra && sudo ./install.sh`
  - or `sudo make install`
- Try it:
  - `hydra spawn feature-x -l dev`
  - `hydra list` • `hydra switch` • `hydra dashboard` • `hydra kill feature-x`

## Why Hydra

- Multiple heads: Isolated tmux sessions + git worktrees per branch.
- GitHub aware: `hydra spawn --issue 123` to branch from an issue.
- Mixed agents: `--ai aider` or `--agents "claude:2,aider:1"` at spawn.
- Dashboard: View panes from many sessions in one place; press `q` to exit.
- Layouts: `default`, `dev`, `full`, and `Ctrl-L` to cycle.
- Durable: `hydra regenerate` restores sessions after restart.

## Demo

<img alt="Hydra" src="assets/demos/quick-tour.gif" width="600" />

If you want to generate GIFs yourself, you can use the [VHS project](https://github.com/charmbracelet/vhs) from Charm

## Core Commands

```sh
# Create a new head for a branch (tmux + worktree)
hydra spawn feature-branch [-l default|dev|full]

# From a GitHub issue
hydra spawn --issue 123

# Bulk and mixed agents
hydra spawn feature -n 3 --ai aider
hydra spawn exp --agents "claude:2,aider:1"

# Inspect & switch
hydra list
hydra switch   # interactive (fzf if available)

# Manage
hydra kill feature-branch
hydra kill --all [--force]

# System
hydra regenerate   # restore sessions after restart
hydra status       # per-head health
hydra doctor       # performance diagnostics

# Dashboard
hydra dashboard
HYDRA_DASHBOARD_PANES_PER_SESSION=2 hydra dashboard
```

## Layouts

- `default`: Single full-screen pane
- `dev`: Two panes (editor ~70% left, terminal right)
- `full`: Three panes (editor top-left, terminal top-right, logs bottom)
- Cycle in-session with `Ctrl-L`.

## Configuration

- `HYDRA_HOME`: Runtime dir (default `~/.hydra`)
- `HYDRA_AI_COMMAND`: Default AI tool (`claude`)
- `HYDRA_ROOT`: Force library discovery when running from source
- `HYDRA_DASHBOARD_PANES_PER_SESSION`: `1`, `N`, or `all`
- `HYDRA_ALLOW_ADVANCED_REFS`: Relax final branch/path charset only; safety checks remain (no whitespace/control, no `..`/`.` components, no `@{`, no trailing `.`/`.lock`, no leading/trailing `/`). Use with care.

Per‑head AI selection is persisted: `hydra spawn <branch> --ai <tool>` shows in `hydra list/status` and is reused by `hydra regenerate`.

## YAML Config (optional)

Place `.hydra/config.yml` in the worktree or repo root to declare windows/panes and optional startup commands:

```yaml
windows:
  - name: editor
    panes:
      - cmd: nvim
      - cmd: bash
        split: v
  - name: server
    panes:
      - cmd: npm run dev
startup:
  - echo "Project ready"
```

- On spawn/regenerate: windows and panes are applied. `startup` runs on spawn, and on regenerate only if `HYDRA_REGENERATE_RUN_STARTUP=1`.
- Minimal parser supports the fields above; values are plain strings.

## Hooks (optional)

Add `.hydra/` scripts to customize lifecycle:

- `hooks/pre-spawn`: runs before tmux session; env: `HYDRA_WORKTREE`, `HYDRA_BRANCH`.
- `hooks/layout`: override built‑in layouts; env: `HYDRA_SESSION`, `HYDRA_WORKTREE`.
- `startup`: one command per line; sent to the main pane after spawn.
- `hooks/post-spawn`: after layout/startup; env: `HYDRA_SESSION`, `HYDRA_WORKTREE`, `HYDRA_BRANCH`.

## Dashboard

- Shows panes from all heads in one tmux window; exits with `q` and restores everything.
- Collect more than one pane per head with `--panes-per-session <N|all>` or `HYDRA_DASHBOARD_PANES_PER_SESSION`.
- More details: `docs/dashboard-demo.md`.

## Development

```sh
make lint    # ShellCheck + dash syntax
make test    # Run tests in tests/*.sh
make help    # Show all targets
```

## Troubleshooting

- Running from source inside a hydra worktree can break library discovery. Fix by installing (`make install`), setting `HYDRA_ROOT=/path/to/hydra`, or invoking the absolute `hydra` path.

## Uninstall

```sh
sudo ./uninstall.sh            # prompts to remove user data
sudo ./uninstall.sh --purge    # non-interactive, remove user data
```

## License

MIT
