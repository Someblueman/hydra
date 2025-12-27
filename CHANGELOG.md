# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
<!-- Nothing yet -->

## [1.3.3] - 2025-12-27

### Added
- TUI Multi-Select for bulk operations
  - `SPACE` - Toggle selection on current session
  - `x` - Bulk kill all selected sessions
  - `G` - Bulk assign group to selected sessions
  - `Esc` - Clear selection (first press) or filters (second press)
  - Visual `[x]` markers for selected items
  - Selection count displayed in header
- New test suites
  - `test_switch.sh` - Input validation tests for cmd_switch
  - `test_json_output.sh` - JSON escaping and output validity tests

### Fixed
- **Security**: Unvalidated input in `cmd_switch` now validates numeric input and range
- **Bug**: `cmd_broadcast` count always showed 0 due to subshell variable loss (now uses temp file)
- **Bug**: `spawn_bulk` off-by-one error caused switch to wrong session (fixed leading space in concatenation)
- **Bug**: `json_escape` didn't handle newlines (now converts to spaces)

### Changed
- Performance: Cache tmux sessions list in `cmd_list` instead of per-entry `tmux has-session`
- Performance: Cache timestamp at command start instead of repeated `date +%s` calls
- Performance: Replace AWK field counting with POSIX shell word splitting in state.sh
- Documentation: Updated README with cleanup, tail, broadcast, wait-idle, group commands
- Documentation: Added missing environment variables to README table
- Documentation: Added `--json` flag to list/status help text

## [1.3.2] - 2025-12-27

### Added
- Session duration tracking
  - Spawn timestamp stored in state file (5th field)
  - `list` and `status` display duration (e.g., "2h 15m")
  - Backward compatible with old state file format
- JSON output mode for scripting/automation
  - `hydra list --json` - machine-readable session list
  - `hydra status --json` - system and session info
  - `hydra list --groups --json` - group summary
- Consistency checks in `hydra doctor`
  - Detects dead sessions (mapping exists, tmux session gone)
  - Detects orphaned worktrees (worktree exists, no mapping)
  - Detects stale locks (older than 60 seconds)
- New `hydra cleanup` command
  - Removes stale locks
  - Cleans dead session mappings
  - Offers to remove orphaned worktrees (interactive)
- State file auto-repair
  - Validates state file on load
  - Backs up and repairs malformed entries automatically

### Fixed
- Fixed `kill --all` not killing tmux sessions due to extra state file fields
- Fixed spacing in list output when AI tool present but no duration

## [1.3.1] - 2025-12-26

### Added
- TUI activity status indicators for sessions

### Changed
- Performance optimizations
  - Lazy library loading reduces startup time
  - In-memory state caching provides O(1) lookups
  - Git worktree list caching with 5-second TTL

## [1.3.0] - 2025-12-23

### Added
- TUI Enhancements
  - Help overlay (`?`): shows all keyboard shortcuts in a centered modal
  - Session tags (`t`): cycle through wip/review/priority tags for selected session
  - Tag filter (`T`): filter sessions by tag
  - Search/filter (`/`): real-time session search by branch name
  - Escape key clears all active filters
  - Progress indicators: visual feedback during spawn/kill/regenerate operations
  - Tags persisted in `~/.hydra/tags` file
- Shell completion dispatcher (`hydra completion <shell>`)
  - New `generate_completion()` function routes to bash/zsh/fish generators
  - Fixes: `hydra completion bash` now works correctly

### Fixed
- Bug #34: Missing `generate_completion` dispatcher caused completion command to fail
- Bug #39: Worktree cleanup now works when invoked outside the main repository
  - Added fallback to `git worktree list --porcelain` for locating worktrees
  - `kill` and `kill --all` now work from any directory

### Changed
- TUI header updated to show new keybindings
- Improved empty state messages in TUI to guide users based on active filters

## [1.2.0] - 2025-08-30

### Added
- Dashboard: configurable multi-pane collection
  - New env `HYDRA_DASHBOARD_PANES_PER_SESSION` controls panes per session:
    - `1` (default): collect first pane only (previous behavior)
    - `N`: collect up to N panes per session
    - `all`: collect all panes from each session (leaves one pane behind)
  - New CLI flag for `dashboard`: `-p, --panes-per-session <N|all>`
  - Pane titles set to branch names for clarity
- Per-head AI persistence in mapping file
  - `spawn` now stores the selected AI tool per head in `~/.hydra/map` (third column)
  - `list` and `status` annotate entries with `[ai: <tool>]`
  - `regenerate` auto-launches the stored AI tool for each restored session
- Concurrency mitigation for session naming
  - Reserve session names using best-effort lock directories under `~/.hydra/locks` during creation
  - Added stale lock cleanup to remove `.lock` dirs older than 24 hours
- Uninstall improvements
  - `uninstall.sh` now detects both default `~/.hydra` and custom `HYDRA_HOME` user data locations
  - New `--purge` flag removes user data non-interactively
 - Safer layout hotkeys
   - `setup_layout_hotkeys` binds `cycle-layout` via absolute hydra path or direct library invocation, reducing PATH injection risk
 - New environment flags for demos/automation
   - `HYDRA_SKIP_AI`: skip AI command launch on spawn
   - `HYDRA_DASHBOARD_NO_ATTACH`: create dashboard without attaching
   - `HYDRA_NO_SWITCH`: create sessions without auto-attaching on spawn

### Changed
- Documentation updated to describe per-head AI persistence and regenerate behavior
- Hardened validation in Git helpers (lib/git.sh): stricter branch and worktree path checks to prevent traversal and injection
 - Layout behavior: new panes inherit the current working directory using tmux `-c`
 - Fallback layouts: splits are anchored to the worktree path (`-c "$wt"`)
 - Dashboard workflow: support non-attaching mode via `HYDRA_DASHBOARD_NO_ATTACH`

### Fixed
- Inconsistent pane directories causing mismatched git branches across panes

### Notes
- Backward compatible with existing two-column map files; missing AI column is handled gracefully
- No changes required for users relying solely on `HYDRA_AI_COMMAND` or `--agents`
- Optional: Set `HYDRA_ALLOW_ADVANCED_REFS=1` to relax conservative charset checks for Git refs while retaining core safety guards

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
[1.2.0]: https://github.com/yourusername/hydra/compare/release/v1.1.0...release/v1.2.0
