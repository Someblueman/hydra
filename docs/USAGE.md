# Command and configuration guide

See `hydra help` for complete command syntax. This guide covers the released local
interface; the [fleet pilot](FLEET.md) is unreleased.


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
hydra list --git        # add recorded-base Git evidence
hydra list -g mygroup   # filter by group
hydra switch feature/ui # enter a named head directly
hydra switch            # interactive (fzf if available)

# Manage
hydra kill feature-branch
hydra kill --all [--force]
hydra cleanup           # stop dead heads; remove stale locks and orphaned worktrees

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

# Durable lifecycle and out-of-band operations
hydra lifecycle feature-x --json
hydra send --type handoff --delivery safe-point feature-x "Ready for review"
hydra outcome feature-x done --actor agent
hydra wait feature-x --for outcome=done --timeout 300
hydra exec --branch feature-x --timeout 300 -- make test
hydra diff feature-x --stat
hydra review feature-x --json
hydra provenance feature-x --json

# Finite trusted workflows
hydra workflow validate examples/workflows/local-review.yml
hydra workflow dry-run examples/workflows/local-review.yml
hydra workflow run examples/workflows/local-review.yml
hydra workflow status run_ID --json
hydra workflow cancel run_ID
hydra workflow resume run_ID

# Parallel safety and guarded integration
hydra claim add feature-x --path 'lib/*' --access write --reason refactor --expires-at 1790000000
hydra scope check feature-x --json
hydra collision feature-x feature-y --json
hydra resource allocate feature-x --port http=3000-3999
hydra gate run feature-x --name acceptance -- make test
hydra context create feature-x --diff --history 5
hydra sync feature-x --from main --gate acceptance --dry-run
hydra land feature-x --into main --gate acceptance --dry-run
hydra integrate release-group --base main --target main --dry-run
hydra integrate release-group --base main --target main --execute --gate 'make test'
hydra integrate train release-group --base main --target main --execute --gate 'make test'
hydra integrate status run_ID
hydra integrate resume run_ID
hydra integrate approve run_ID --by reviewer
hydra integrate promote run_ID       # local promotion; never pushes
hydra integrate cleanup run_ID --apply
hydra du
hydra gc --policy orphaned --dry-run
hydra worktree doctor status

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
hydra snapshot --native # explicitly try the optional read-only native helper

# Dashboard & TUI
hydra dashboard                                    # multi-session overview
hydra tui                                          # native mission control; visible basic fallback
hydra tui --basic                                  # explicit basic shell TUI
hydra tui --capabilities                           # native/basic diagnostics
```

## Shell completions

```sh
# Bash
hydra completion bash >> ~/.bashrc

# Zsh: ensure this directory is on fpath before compinit
mkdir -p ~/.zsh/completions
hydra completion zsh > ~/.zsh/completions/_hydra

# Fish
mkdir -p ~/.config/fish/completions
hydra completion fish > ~/.config/fish/completions/hydra.fish
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
| `HYDRA_NO_SWITCH` | Set to `1` to create a head without attaching |
| `HYDRA_HOME` | Runtime dir (default `~/.hydra`) |
| `HYDRA_AI_COMMAND` | Default agent override; project profiles are preferred |
| `HYDRA_ROOT` | Force library discovery when running from source |
| `HYDRA_DASHBOARD_PANES_PER_SESSION` | `1`, `N`, or `all` |
| `HYDRA_SKIP_AI` | Non-interactive shell-only default; `spawn --no-agent` is explicit |
| `HYDRA_DASHBOARD_NO_ATTACH` | Create dashboard without attaching |
| `HYDRA_NONINTERACTIVE` | Skip all confirmation prompts (for CI/automation) |
| `HYDRA_REGENERATE_RUN_STARTUP` | Run startup commands on regenerate |
| `HYDRA_ALLOW_ADVANCED_REFS` | Relax branch charset validation (use with care) |
| `HYDRA_DISABLE_YAML` | Disable YAML config parsing |
| `HYDRA_CORE` | Explicit optional `hydra-core` executable for native qualification |
| `HYDRA_CORE_TIMEOUT_SECONDS` | Native handshake/command timeout (default `2`) |
| `HYDRA_TUI_BIN` | Explicit optional `hydra-tui` executable for qualification |

Per-head profile, task, identity, worktree path, instance, and lifecycle event are
stored in project-scoped state v2. See [profiles](PROFILES.md),
[state](STATE.md), [events](EVENTS.md), and
[automation](AUTOMATION.md). Security, out-of-band execution, provenance, and
related contracts are documented in [security](SECURITY.md),
[operations](OPERATIONS.md), and [provenance](PROVENANCE.md).
Parallel coordination and native distribution are documented in
[parallel safety](PARALLEL_SAFETY.md), [workflows and verified
integration](workflows.md), and the
[optional native core](NATIVE_CORE.md). Native/basic dispatch, terminal safety,
keymaps, accessibility, and recovery are documented in
[native mission control](NATIVE_TUI.md).

Supported systems and upgrade policy are in [docs/SUPPORT.md](SUPPORT.md).
Existing 1.9 installations should follow
[docs/MIGRATING_TO_2.0.md](MIGRATING_TO_2.0.md) before removing their state
backup.

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

Hydra launches native mission control by default when its qualified executable is
available and falls back visibly to the basic shell TUI over the same shell-owned
state.

The source installer builds the native TUI automatically when a C99 toolchain is
available. Set `HYDRA_INSTALL_TUI=never` for a compiler-free shell-only install.

```sh
hydra tui                  # native-first with visible basic fallback
hydra tui --basic          # explicit basic mode
hydra tui --capabilities   # availability and observation diagnostics
```

The native keymap is deliberately small: `j`/`k` or arrows navigate, `Enter` opens
detail, `v` cycles views, `/` searches heads, `:` searches explicit actions, `p`
opens terminal output, `d` toggles diagnostics, `Esc` returns to heads, `?` opens
keyboard help, and `q` exits. The action
palette includes the tmux dashboard. Mutations are delegated to the shell CLI with
argument-vector execution; native spawn prompts for branch, profile, template, and
layout, while `Space`/`A` select heads and `G` assigns the selection to a group. `x`
kills selected heads through the confirming shell command and skips the current tmux
session. See [Native mission control](NATIVE_TUI.md).

The larger keymap below belongs to the maintained basic shell TUI.

| Key | Action |
|-----|--------|
| `j/k`, arrows | Navigate sessions |
| `Enter` / `s` | Switch to selected session |
| `n` | Spawn new session (interactive wizard) |
| `d` | Kill selected session |
| `D` | Open tmux dashboard |
| `A` | Select all sessions |
| `Space` | Toggle selection (for bulk ops) |
| `x` | Bulk kill selected |
| `G` | Bulk set group on selected |
| `p` | Toggle preview panel |
| `/` | Search (branch, session, group, AI) |
| `i` | Show status output |
| `r` | Regenerate sessions |
| `?` | Show help overlay |
| `Esc` | Clear selection / filters |
| `q` | Quit |

Environment variables: `HYDRA_TUI_PREVIEW_LINES`, `HYDRA_TUI_REFRESH_MS`, `HYDRA_TUI_ACTIVITY_INTERVAL`, `HYDRA_NONINTERACTIVE`.

