# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2025-07-03

### Added
- GitHub Issue Integration (`hydra spawn --issue <#>`)
  - Create heads directly from GitHub issues
  - Automatically generates branch names from issue titles
  - Validates issue numbers and fetches issue details via GitHub CLI
- Bulk spawn capability for multi-agent workflows
  - `hydra spawn <branch> -n <count>` creates multiple numbered sessions
  - `hydra spawn <branch> --agents "claude:2,aider:1"` for mixed AI agents
  - Automatic rollback on failure with confirmation prompts
- Kill all sessions command (`hydra kill --all`)
  - Optional `--force` flag to skip confirmation
  - Safely removes all sessions with proper cleanup
- Support for Google Gemini CLI as an AI tool option
  - Users can now spawn sessions with `--ai gemini`
  - Gemini provides free access with generous limits (60 req/min, 1000 req/day)
  - Added gemini to mixed agents support (e.g., `--agents "claude:2,gemini:1"`)
  - Requires Node.js 18+ and Google account authentication

### Changed
- Enhanced library path resolution for better reliability
  - Support for running hydra from inside hydra-managed sessions
  - Multiple fallback paths with HYDRA_ROOT environment variable
- Updated AI tool validation to include gemini
- Enhanced shell completions for all supported shells (bash, zsh, fish)
- Updated documentation with gemini requirements and examples
- Version bumped to 1.1.0-dev during development cycle

### Fixed
- Library path resolution when running hydra inside sessions
- Non-interactive mode handling in delete_worktree
- Test cleanup to prevent HYDRA_NONINTERACTIVE state leak
- ShellCheck compliance improvements (removed grep|wc -l patterns)

## [0.2.0] - 2025-06-18

### Added
- Multi-AI tool support with `HYDRA_AI_COMMAND` environment variable
  - Support for claude, codex, cursor, copilot, aider, and custom commands
  - Whitelist-based command validation for security
- Security hardening for branch names and paths
  - Protection against command injection
  - Path traversal prevention
  - Option injection protection in git commands
- Install and uninstall scripts with root permission checks
- MIT License file

### Changed
- Updated documentation to reflect multi-AI support
- Improved test robustness for non-terminal environments
- Enhanced error handling for missing command arguments

### Fixed
- Dashboard test failures with branch names containing slashes
- Git branch existence check using incorrect rev-parse syntax
- Test assertion checking for shell-specific error messages
- Race condition awareness documented for session naming

## [0.1.0] - 2025-06-18

### Added
- Initial release of Hydra
- Core POSIX-compliant shell CLI wrapping tmux ≥ 3.0 and git worktree
- Main commands:
  - `spawn` - Create new branch with dedicated tmux session and worktree
  - `list` - Show all active Hydra heads with their status
  - `switch` - Interactive session switching with fzf or numeric selection
  - `kill` - Remove session and worktree for a branch
  - `regenerate` - Recreate missing sessions from saved mappings
  - `status` - Quick status overview
  - `doctor` - System health diagnostics and performance testing
  - `cycle-layout` - Cycle through tmux pane layouts (Ctrl-L hotkey)
- Dashboard command for unified session monitoring
- Three built-in layouts: default, dev, and full
- Session state persistence in `~/.hydra/map`
- Layout persistence and restoration
- Shell completion support for bash, zsh, and fish
- Comprehensive test suite with 107 tests
- Full documentation including README and CLAUDE.md

### Fixed
- Library path resolution for installed executable
- POSIX compliance issues in initial implementation
- Critical unbound variable errors

[0.2.0]: https://github.com/yourusername/hydra/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yourusername/hydra/releases/tag/v0.1.0