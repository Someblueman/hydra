# Hydra

A POSIX-compliant CLI tool that wraps tmux ≥ 3.0 and git worktree to manage parallel Claude-Code sessions ("heads").

## Features

- **Multiple Parallel Sessions**: Work on different branches simultaneously with isolated environments
- **tmux Integration**: Each branch gets its own tmux session with customizable layouts
- **Git Worktree Management**: Automatic creation and cleanup of git worktrees
- **Session Persistence**: Maintains branch-to-session mappings across restarts
- **Interactive Switching**: Use fzf for quick session switching (falls back to numeric selection)
- **Performance Monitoring**: Built-in latency tracking ensures <100ms switch times

## Requirements

- `/bin/sh` (POSIX shell)
- git
- tmux ≥ 3.0
- Claude CLI (optional but recommended)
- fzf (optional, for interactive switching)

## Installation

```sh
git clone https://github.com/yourusername/hydra.git
cd hydra
sudo ./install.sh
```

Or use make:

```sh
sudo make install
```

To uninstall:

```sh
sudo ./uninstall.sh
```

## Usage

### Basic Commands

```sh
# Create a new head for a branch
hydra spawn feature-branch

# List all active heads
hydra list

# Switch between heads (interactive)
hydra switch

# Kill a head (removes session and worktree)
hydra kill feature-branch

# Regenerate sessions after restart
hydra regenerate

# Check system health
hydra status
hydra doctor

# View all sessions in a dashboard
hydra dashboard

# Cycle through layouts in current session
hydra cycle-layout
```

### Spawn Options

```sh
# Spawn with specific layout
hydra spawn feature-branch --layout dev   # Two panes: editor (70%) + terminal (30%)
hydra spawn feature-branch --layout full  # Three panes: editor + terminal + logs
hydra spawn feature-branch --layout default  # Single pane (default)
```

### Layouts

Hydra supports three built-in layouts:

1. **default**: Single pane, full screen
2. **dev**: Two panes - editor (left 70%) and terminal (right 30%)
3. **full**: Three panes - editor (top-left), terminal (top-right), logs (bottom)

Press `Ctrl-L` within a session to cycle through layouts.

### Dashboard

The dashboard provides a unified view of all active Hydra sessions:

```sh
# View all sessions in a single dashboard
hydra dashboard

# Get help about dashboard
hydra dashboard --help
```

Dashboard features:
- Displays panes from all active sessions in a grid layout
- Press `q` to exit and restore panes to their original sessions
- Non-disruptive: panes are temporarily moved and restored on exit
- Automatically adjusts layout based on number of sessions (2x2, 3x3, etc.)

## How It Works

1. **Spawn**: Creates a git worktree at `../hydra-{branch}` and a tmux session
2. **Mapping**: Stores branch-to-session mappings in `~/.hydra/map`
3. **Switch**: Uses tmux's `switch-client` for instant context switching
4. **Persistence**: Mappings survive system restarts; use `regenerate` to restore sessions

## Environment Variables

- `HYDRA_HOME`: Directory for runtime files (default: `~/.hydra`)

## Performance

Hydra targets <100ms switch latency. Run `hydra doctor` to test your system's performance.

## POSIX Compliance

Hydra is strictly POSIX-compliant and works with `/bin/sh`. It avoids:
- Bash/Zsh specific features (arrays, `[[`, process substitution)
- Non-POSIX utilities
- GNU-specific options

All scripts pass validation with:
- ShellCheck (`--shell=sh --severity=style`)
- dash syntax checking (`dash -n`)

## Development

```sh
# Install hydra (requires sudo)
sudo make install

# Run linter (requires shellcheck)
make lint

# Run tests
make test

# Clean temporary files
make clean

# Show all available targets
make help
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for POSIX shell guidelines.

## License

MIT