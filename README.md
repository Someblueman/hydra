# Hydra

**Native mission control for coding agents, worktrees, and remote fleets.**

Hydra coordinates coding agents across local projects and remote SSH hosts. Give
each task its own branch, working directory, and terminal session, monitor work
from the native TUI, and bring changes together through review and verification.

It runs on macOS and Linux without a daemon, database, or cloud account. Git holds
your code, tmux keeps sessions running, and SSH connects your hosts. You can also
use ordinary shell sessions alongside agents.

[![CI](https://github.com/Someblueman/hydra/workflows/CI/badge.svg)](https://github.com/Someblueman/hydra/actions)
[![Release](https://img.shields.io/github/v/release/Someblueman/hydra)](https://github.com/Someblueman/hydra/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

![Hydra: native mission control, real agent output, and remote fleet attach](assets/demos/quick-tour.gif)

[Demo transcript and recording instructions](assets/demos/README.md)

## What you can do

- **Lead from native mission control.** Browse heads, inspect activity and changes,
  and launch actions from the native C TUI. The shell TUI provides a fallback.
- **Run coding agents across your fleet.** Select agent profiles and task prompts,
  bootstrap trusted SSH hosts, launch remote heads and workflows, and attach to
  their sessions. Inspect host-qualified heads in the native fleet view.
- **Work in parallel.** Create isolated heads with named agent profiles, task
  prompts, and terminal layouts. Switch between them without changing directories
  or disturbing another task's working tree.
- **See what is happening.** Inspect sessions from native mission control, a tmux
  dashboard, or the CLI. Query structured state and retain lifecycle history.
- **Coordinate work.** Run finite workflows, exchange messages, declare file
  scopes, and detect collisions between heads.
- **Review and integrate.** Inspect diffs and provenance, run verification gates,
  and use guarded integration commands to assemble changes.

Hydra coordinates processes and records evidence. Agent activity does not imply
that a task is complete or that its changes are correct.

## Installation

You need Git, tmux 3.0 or newer, Make, and a C99 compiler. Fleet additionally needs
`pkg-config` and JSON-C development files (`brew install json-c pkg-config` on
macOS, or `apt install libjson-c-dev pkg-config` on Ubuntu). The installer builds
the native TUI; build the fleet coordinator before installing.

```sh
git clone https://github.com/Someblueman/hydra.git
cd hydra
make build-fleet
PREFIX="$HOME/.local" ./install.sh
export PATH="$HOME/.local/bin:$PATH"
hydra version
hydra doctor
```

Add `$HOME/.local/bin` to your shell's `PATH` to keep the command available in new
terminals. You can also run `bin/hydra` directly from a checkout without installing.
GitHub CLI, `fzf`, and coding agents are optional integrations. For a compiler-free
local installation, skip `make build-fleet` and set `HYDRA_INSTALL_TUI=never` when
running the installer.

Upgrading from 1.9? Follow the [2.0 migration guide](docs/MIGRATING_TO_2.0.md),
including its backup and verification steps. See [platform support](docs/SUPPORT.md)
for installation and upgrade guarantees.

## Start a task

From a Git repository with an initial commit:

```sh
hydra init --no-agent --trust
HYDRA_NO_SWITCH=1 hydra spawn feature/search --no-agent
hydra list
hydra switch feature/search
```

A *head* is a branch with its own worktree and tmux session. `spawn` normally
attaches immediately; `HYDRA_NO_SWITCH=1` leaves you in the current terminal.
`switch` attaches to the named session. Detach with `Ctrl-b`, then `d`, to return to your original terminal.
`init --trust` accepts the current repository-controlled Hydra configuration;
review existing configuration before accepting it.

To use a coding agent, select an installed profile explicitly:

```sh
hydra agent list
hydra spawn feature/tests --profile codex --prompt "Add tests for search"
```

Agents run in the head's worktree. Choose `--no-agent` whenever you want a regular
shell. [Profiles and task inputs](docs/PROFILES.md) describes selection and setup.

## Inspect and finish

```sh
hydra tui                         # native mission control, with basic fallback
hydra diff feature/search --stat
hydra exec --branch feature/search -- make test
hydra review feature/search --json
```

Use `hydra tui --basic` for the shell interface. In native mission control,
`j`/`k` navigate, `Enter` opens details, `p` shows terminal output, and `:` opens
actions. Use `d` for diagnostics, `Esc` to return to heads, `?` for help, and `q`
to exit. The [dashboard](docs/dashboard-demo.md) brings live panes into one tmux view.

After reviewing and preserving the work you need, stop the head from your original
terminal:

```sh
hydra kill feature/search
```

Stopping a head removes its session and worktree and retains lifecycle history.
Its Git branch remains available. See [operations](docs/OPERATIONS.md) and
[verified integration](docs/workflows.md) for review, recovery, and landing changes.

## Manage a fleet

Register trusted SSH hosts, bootstrap a pinned Hydra package, and coordinate work
across their existing repositories. Fleet supports remote agent-backed heads,
workflow execution and cancellation, interactive attach, and explicit transfers of
configuration and workflow history.

```sh
hydra remote add build ubuntu@build-host
# Bootstrap a matching package as described in the fleet setup guide.
hydra fleet list --json
hydra fleet spawn build --project /srv/project -- feature/search --profile codex \
  --prompt "Implement search and run the project tests"
hydra fleet tui
```

The native fleet view shows heads by host and supports attach and confirmed
interrupts. Agent executables and repositories live on the host that runs the work.
See [fleet setup](docs/FLEET.md) for pinned bootstrap, host requirements, remote
workflows, and recovery behavior.

## Documentation

| Topic | Guide |
| --- | --- |
| Commands, layouts, hooks, configuration, and TUI keys | [Usage](docs/USAGE.md) |
| Remote hosts, bootstrap, and fleet operations | [Fleet](docs/FLEET.md) |
| Agent profiles and prompts | [Profiles](docs/PROFILES.md) |
| Workflows and guarded integration | [Workflows](docs/workflows.md) |
| Scopes, collisions, resources, and gates | [Parallel safety](docs/PARALLEL_SAFETY.md) |
| Lifecycle, messaging, and automation | [Automation](docs/AUTOMATION.md) · [Events](docs/EVENTS.md) |
| State, recovery, and provenance | [State](docs/STATE.md) · [Operations](docs/OPERATIONS.md) · [Provenance](docs/PROVENANCE.md) |
| Native helpers and terminal behavior | [Native core](docs/NATIVE_CORE.md) · [Native TUI](docs/NATIVE_TUI.md) |
| Public interfaces and trust boundaries | [Contracts](docs/CONTRACTS.md) · [Security](docs/SECURITY.md) |
| Releases and upcoming work | [Release notes](RELEASE_NOTES.md) · [Changelog](CHANGELOG.md) · [Roadmap](docs/ROADMAP.md) |

## Development

The CLI and lifecycle orchestration use POSIX shell; optional native helpers use C.
Source qualification needs GNU Make, ShellCheck, dash, Git, tmux, and a C compiler.
The fleet build additionally needs pkg-config and JSON-C development files.

```sh
make lint       # ShellCheck and shell syntax
make test-all   # Complete acceptance suite, including native and PTY checks
make sanitize   # Native sanitizer checks
make help       # Build, package, and focused test targets
```

Contributions should include checks appropriate to their scope. Versions after
2.0 are chosen at release time from compatibility impact; see the
[release policy](docs/VERSIONING.md).

## Uninstall

Use the same prefix as installation:

```sh
PREFIX="$HOME/.local" ./uninstall.sh
```

The uninstaller prompts about removing user data. See `./uninstall.sh --help` for
options before choosing a non-interactive purge.

## License

[MIT](LICENSE)
