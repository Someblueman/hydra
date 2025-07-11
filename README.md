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
        <b>A POSIX-compliant CLI tool that wraps tmux ≥ 3.0 and git worktree to manage parallel AI coding sessions ("heads")</b>
    </p>
    <p>
        Work on multiple git branches simultaneously, each with its own tmux session and isolated worktree.
    </p>
</div>


## Features

- **Multiple Parallel Sessions**: Work on different branches simultaneously with isolated environments
- **tmux Integration**: Each branch gets its own tmux session with customizable layouts
- **Git Worktree Management**: Automatic creation and cleanup of git worktrees
- **GitHub Integration**: Create heads directly from GitHub issues
- **Session Persistence**: Maintains branch-to-session mappings across restarts
- **Interactive Switching**: Use fzf for quick session switching (falls back to numeric selection)
- **Performance Monitoring**: Built-in latency tracking ensures <100ms switch times

## Quick Start

### Requirements

- `/bin/sh` (POSIX shell)
- git
- tmux ≥ 3.0
- AI CLI tool (optional but recommended: claude, codex, cursor, copilot, aider, or gemini)
  - Note: gemini requires Node.js 18+ and Google account authentication
- fzf (optional, for interactive switching)
- GitHub CLI (optional, for GitHub issue integration)

### Installation

```sh
git clone https://github.com/Someblueman/hydra.git
cd hydra
sudo ./install.sh
```

Or using make:

```sh
sudo make install
```

## Usage

### Essential Commands

```sh
# Create a new head for a branch
hydra spawn feature-branch

# Create a head from a GitHub issue
hydra spawn --issue 123

# List all active heads
hydra list

# Switch between heads interactively
hydra switch

# Kill a head (removes session and worktree)
hydra kill feature-branch

# View all sessions in a unified dashboard
hydra dashboard
```

### Working with Layouts

Hydra supports three built-in layouts that can be specified during spawn or cycled through with `Ctrl-L`:

```sh
# Spawn with specific layout
hydra spawn feature-branch --layout dev      # Two panes: editor (70%) + terminal (30%)
hydra spawn feature-branch --layout full     # Three panes: editor + terminal + logs
hydra spawn feature-branch --layout default  # Single pane (default)

# Cycle through layouts in current session
hydra cycle-layout
```

**Available layouts:**
- `default`: Single pane, full screen
- `dev`: Two panes - editor (left 70%) and terminal (right 30%)
- `full`: Three panes - editor (top-left), terminal (top-right), logs (bottom)

### Dashboard View

The dashboard provides a unified view of all active Hydra sessions in a single tmux window:

```sh
hydra dashboard        # View all sessions in a grid
hydra dashboard --help # Get help about dashboard features
```

**Dashboard features:**
- Displays panes from all active sessions in a grid layout
- Press `q` to exit and restore panes to their original sessions
- Non-disruptive: panes are temporarily moved and restored on exit
- Automatically adjusts layout based on number of sessions (2x2, 3x3, etc.)

### System Management

```sh
# Regenerate sessions after restart
hydra regenerate

# Check system health and performance
hydra status
hydra doctor
```

## Examples

### Dashboard with Multiple Sessions

The dashboard provides a unified view of all your active Hydra sessions:

<img src="assets/dashboard.png" alt="Hydra Dashboard" width="800">

### Development Layout

The `dev` layout provides a split view perfect for coding with an editor and terminal:

<img src="assets/dev_layout.png" alt="Development Layout" width="800">

### Full Layouts

Choose from multiple full layout options for complex workflows:

**Quad Layout (2x2 grid):**
<img src="assets/full_quad.png" alt="Quad Layout" width="800">

**Triple Layout (1 large + 2 small panes):**
<img src="assets/full_triple.png" alt="Triple Layout" width="800">

**Six Panes Layout:**
<img src="assets/full_six_panes.png" alt="Six Panes Layout" width="800">

## How It Works

1. **Spawn**: Creates a git worktree at `../hydra-{branch}` and a tmux session
2. **Mapping**: Stores branch-to-session mappings in `~/.hydra/map`
3. **Switch**: Uses tmux's `switch-client` for instant context switching
4. **Persistence**: Mappings survive system restarts; use `regenerate` to restore sessions

## Development

Hydra is strictly POSIX-compliant and works with `/bin/sh`. All scripts are validated with ShellCheck and dash.

```sh
# Run linter (requires shellcheck)
make lint

# Run tests
make test

# Clean temporary files
make clean

# Show all available targets
make help
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed development guidelines.

## Configuration

**Environment Variables:**
- `HYDRA_HOME`: Directory for runtime files (default: `~/.hydra`)
- `HYDRA_AI_COMMAND`: Default AI tool to use (default: `claude`)
- `HYDRA_ROOT`: Override hydra installation path for library discovery (useful when running from source)

## Performance

Hydra targets <100ms switch latency. Run `hydra doctor` to test your system's performance and identify any bottlenecks.

## Troubleshooting

### Hydra commands fail inside hydra sessions

If you're running hydra from source and commands like `hydra list` fail when run from inside a hydra-managed session, this is likely due to library path resolution issues. Solutions:

1. **Install hydra system-wide**: Run `make install` to install hydra to `/usr/local/bin`
2. **Set HYDRA_ROOT**: Export `HYDRA_ROOT=/path/to/hydra` pointing to your hydra source directory
3. **Use absolute paths**: Call hydra using its full path instead of relying on PATH

The issue occurs because hydra sessions run in git worktrees that don't contain the library files needed by the hydra script.

## License

MIT
