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

Five-minute tour: install or run from source, verify, create a disposable head, then clean it up. No root and no agent CLI required.

- Requirements: `git`, `tmux` (≥ 3.0). Optional: `fzf`, GitHub CLI, an AI CLI (`claude`, `aider`, `gemini`, etc.).
- Supported platforms: Linux and macOS; POSIX `sh` (dash on Debian/Ubuntu); `tmux >= 3.0` and `git`. CI runs Ubuntu and macOS. Windows is not supported.
- Public CLI, map, JSON, lock, install, and TUI-row contracts: [docs/CONTRACTS.md](docs/CONTRACTS.md).

### 1. Install (or run from source)

Non-root install to a writable prefix:

```sh
git clone https://github.com/Someblueman/hydra && cd hydra
PREFIX=$HOME/.local ./install.sh
# or: make install PREFIX=$HOME/.local
export PATH="$HOME/.local/bin:$PATH"
```

`sudo ./install.sh` and `sudo make install` still install to `/usr/local` (the default `PREFIX`).

Or skip install and run from the checkout:

```sh
git clone https://github.com/Someblueman/hydra && cd hydra
export HYDRA_ROOT="$PWD"
export PATH="$HYDRA_ROOT/bin:$PATH"
hydra version
```

### 2. Verify

```sh
hydra version          # Hydra version 1.5.2
hydra doctor           # install paths, git/tmux, writable HYDRA_HOME, agents
```

### 3. Create a throwaway first head

Use a disposable repository so Hydra does not create branches in a real project. No agent CLI is required:

```sh
mkdir /tmp/hydra-tour && cd /tmp/hydra-tour
git init && git commit --allow-empty -m "tour"
hydra init --no-agent --trust
hydra spawn first-head --no-agent --prompt "Inspect this throwaway project"
hydra list
hydra path first-head   # stored identity-scoped worktree path
hydra switch           # enter the session (fzf if installed)
```

`--no-agent` is the first-class shell-only path. Without an explicit or configured
profile, Hydra selects one only when exactly one supported executable is positively
detected; zero agents select `none`, while multiple agents require an explicit choice.

### 4. Clean up

```sh
hydra kill first-head
git branch -D first-head # remove the disposable tour branch
hydra list             # empty
```

That leaves no Hydra tmux session, worktree, branch, or `HYDRA_HOME` map entry for `first-head`.

### Shell completions

Write completions into your user files (no root):

```sh
# bash
hydra completion bash >> ~/.bashrc

# zsh (ensure the directory is on fpath)
mkdir -p ~/.zsh/completions
hydra completion zsh > ~/.zsh/completions/_hydra

# fish
mkdir -p ~/.config/fish/completions
hydra completion fish > ~/.config/fish/completions/hydra.fish
```

## Demo

<img alt="Hydra" src="assets/demos/quick-tour.gif" width="600" />

If you want to generate GIFs yourself, you can use the [VHS project](https://github.com/charmbracelet/vhs) from Charm

## Core Commands

```sh
# Create a new head for a branch (tmux + worktree)
hydra spawn feature-branch [-l default|dev|full]
hydra spawn feature-branch --dry-run --no-agent
hydra spawn feature-branch --profile claude --prompt "Implement the task"
hydra spawn feature-branch --profile codex --prompt-file task.md

# From a GitHub issue
hydra spawn --issue 123

# Bulk and mixed agents
hydra spawn feature -n 3 --ai aider
hydra spawn exp --agents "claude:2,aider:1"

# Inspect & switch
hydra list              # list all sessions
hydra list --json       # JSON output for scripting
hydra list -g mygroup   # filter by group
hydra switch            # interactive (fzf if available)

# Manage
hydra kill feature-branch
hydra kill --all [--force]
hydra cleanup           # remove dead mappings, stale locks, orphaned worktrees

# Group operations
hydra group feature-x backend    # assign to group
hydra group feature-x            # show group
hydra group feature-x --clear    # remove from group

# Session output
hydra tail feature-x             # view last 50 lines of session output
hydra tail feature-x -f          # follow session output continuously
hydra broadcast "make test"      # send command to all sessions
hydra broadcast -g backend "..."  # send to specific group
hydra wait-idle                  # wait for sessions to become idle
hydra wait-idle -g backend -s 10 # wait for group with 10s idle threshold

# System
hydra init --profile claude --trust
hydra agent list
hydra capabilities --json
hydra state verify
hydra events tail
hydra regenerate   # restore sessions after restart
hydra status       # per-head health
hydra status --json # JSON output
hydra doctor       # install, dependencies, and first-run readiness

# Dashboard & TUI
hydra dashboard                                    # multi-session overview
hydra tui                                          # interactive session manager
```

## Layouts

- `default`: Single full-screen pane
- `dev`: Two panes (editor ~70% left, terminal right)
- `full`: Three panes (editor top-left, terminal top-right, logs bottom)
- Cycle in-session with `Ctrl-L`.

## Configuration

### Environment Variables

| Variable | Description |
|----------|-------------|
| `HYDRA_HOME` | Runtime dir (default `~/.hydra`) |
| `HYDRA_AI_COMMAND` | Legacy default agent override; prefer project profiles |
| `HYDRA_ROOT` | Force library discovery when running from source |
| `HYDRA_DASHBOARD_PANES_PER_SESSION` | `1`, `N`, or `all` |
| `HYDRA_SKIP_AI` | Legacy shell-only switch; prefer `spawn --no-agent` |
| `HYDRA_DASHBOARD_NO_ATTACH` | Create dashboard without attaching |
| `HYDRA_NONINTERACTIVE` | Skip all confirmation prompts (for CI/automation) |
| `HYDRA_REGENERATE_RUN_STARTUP` | Run startup commands on regenerate |
| `HYDRA_ALLOW_ADVANCED_REFS` | Relax branch charset validation (use with care) |
| `HYDRA_DISABLE_YAML` | Disable YAML config parsing |

Per-head profile, task, identity, worktree path, instance, and lifecycle event are
stored in project-scoped state v2. See [profiles](docs/PROFILES.md),
[state](docs/STATE.md), [events](docs/EVENTS.md), and
[automation](docs/AUTOMATION.md).

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

- Repository-controlled setup is inert until its exact config hash is accepted with
  `hydra init --trust`; changing the file requires renewed trust.
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

## TUI

Interactive terminal UI for managing sessions with real-time updates.

```sh
hydra tui
```

| Key | Action |
|-----|--------|
| `j/k`, arrows | Navigate sessions |
| `Enter` / `s` | Switch to selected session |
| `n` | Spawn new session (interactive wizard) |
| `d` | Kill selected session |
| `D` | Open tmux dashboard |
| `a` | Kill all sessions |
| `A` | Select all sessions |
| `Space` | Toggle selection (for bulk ops) |
| `x` | Bulk kill selected |
| `G` | Bulk set group on selected |
| `p` | Toggle preview panel |
| `f` | Toggle preview follow mode |
| `t` | Cycle tag on session |
| `T` | Filter by tag |
| `/` | Search (branch, session, group, AI) |
| `i` | Show status output |
| `r` | Regenerate sessions |
| `?` | Show help overlay |
| `Esc` | Clear selection / filters |
| `q` | Quit |

Environment variables: `HYDRA_TUI_PREVIEW_LINES`, `HYDRA_TUI_REFRESH_MS`, `HYDRA_TUI_ACTIVITY_INTERVAL`, `HYDRA_NONINTERACTIVE`.

## Development

```sh
make lint    # ShellCheck + dash syntax
make test    # Run tests in tests/*.sh
make help    # Show all targets
```

## Uninstall

Use the same `PREFIX` you installed with:

```sh
PREFIX=$HOME/.local ./uninstall.sh            # prompts to remove user data
PREFIX=$HOME/.local ./uninstall.sh --purge    # non-interactive, remove user data
# or: make uninstall PREFIX=$HOME/.local
```

Default `PREFIX` is `/usr/local` (may need `sudo` if you installed there).

## License

MIT
