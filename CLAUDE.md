# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Hydra is a POSIX-compliant shell CLI that wraps tmux ≥ 3.0 and git worktree to manage parallel Claude-Code sessions. It enables developers to work on multiple git branches simultaneously, each with its own tmux session and isolated worktree.

## Key Commands

```sh
# Lint all shell scripts for POSIX compliance
make lint

# Run tests
make test

# Install to /usr/local/bin
make install
```

## Architecture

### Core Components

1. **bin/hydra** - Main executable that dispatches commands
2. **lib/git.sh** - Git worktree management functions
3. **lib/tmux.sh** - tmux session management functions
4. **lib/layout.sh** - Layout management for tmux panes
5. **lib/state.sh** - Persistence layer for branch-session mappings

### State Management

- Mappings stored in `$HYDRA_HOME/map` (default: `~/.hydra/map`)
- Format: newline-separated `branch session` pairs
- No JSON, no databases - simple text files for POSIX compliance

### Command Flow

1. **spawn**: create_worktree() → generate_session_name() → create_session() → add_mapping()
2. **switch**: list active sessions → fzf/numeric selection → switch_to_session()
3. **kill**: get_session_for_branch() → kill_session() → delete_worktree() → remove_mapping()
4. **regenerate**: iterate mappings → create missing sessions → cleanup invalid mappings

## POSIX Compliance Requirements

**CRITICAL**: All code must be strictly POSIX-compliant:

- Use `#!/bin/sh` (never bash/zsh)
- No arrays, use positional parameters with `set --`
- No `[[`, use single brackets `[` with proper quoting
- No process substitution `<()`, use temp files
- No `let` or `(())`, use `$(())` for arithmetic
- Use `command -v` instead of `which`
- Must pass `shellcheck --shell=sh --severity=style`
- Must pass `dash -n` validation

## Testing Strategy

When modifying code:
1. Run `make lint` after every change
2. Test with both sh and dash
3. Verify tmux integration manually
4. Check performance with `hydra doctor`

## Common Pitfalls to Avoid

1. **Variable scope in pipes**: Variables set in pipes are lost in parent shell
2. **Worktree paths**: Use absolute paths to avoid issues with relative references
3. **Session names**: Must be tmux-safe (alphanumeric, underscore, dash)
4. **Error handling**: Always check return codes, especially for git/tmux commands