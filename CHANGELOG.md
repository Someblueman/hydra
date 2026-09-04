# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-09-04

### Added

- Native TUI keyboard help, a dashboard palette action, and structured spawn prompts
  for branch, profile, template, and layout, all delegated to the shell CLI
- Native TUI branch-stable multi-selection, select-all, and bulk group assignment
  through explicit shell CLI arguments
- Native TUI bulk kill through the public confirming shell command, with a bounded
  tmux lookup that excludes the current session

### Changed

- `hydra tui` now launches native mission control by default when available and falls
  back visibly to `hydra tui --basic`
- Source installation auto-builds the native TUI when a C99 toolchain is available;
  `HYDRA_INSTALL_TUI=never` preserves a compiler-free shell-only install
- Hydra, native core, and native TUI version handshakes now report 2.0.0; protocol
  versions remain unchanged
- State-v2 per-head scalar records are now the only runtime authority for list,
  status, group, dependency, messaging, limits, maintenance, dashboard, completion,
  basic TUI, and native TUI readers
- The stable 2.0 contracts now define JSON/error negotiation, lifecycle and workflow
  evidence, native/basic parity, supported platforms, upgrades, deprecation windows,
  and the consolidated local trust model

### Removed

- Basic TUI tag mutation and filtering, including its separate non-authoritative tag
  store
- The basic TUI's one-key kill-all shortcut; select-all plus confirmed bulk kill and
  the public `hydra kill --all` command remain available
- The redundant preview-follow toggle; preview content continues to update on the
  normal bounded refresh cycle
- The redundant `hydra tui --native` mode now that native mission control is the
  default
- Runtime dual-writing and reading of the seven-field project `compat-map`, the
  global map fallback, and the in-process state-map cache

### Migration

- `hydra state migrate --dry-run` verifies state v2 and every retained 1.9
  compatibility projection without mutation
- `hydra state migrate` backs up the full state tree, removes only verified 1.9
  projections and the inactive global map, then verifies state again
- `hydra state rollback` fails closed around active state/event writers and restores
  1.9 projections only for an intentional downgrade

## [1.9.0] - 2026-09-02

### Added

- Strict static workflow schema v1 with trusted lookup, validation, dry-run, durable
  run manifests, bounded fan-out/join execution, cancellation, and crash recovery
- Explicit idempotency and retry policy, per-run parallelism, stable scheduling,
  resource bounds, disk safeguards, correlated events, and authoritative attempts
- Isolated verified integration worktrees with candidate/base bindings, conflict
  preview, verification reports, explicit approval, local promotion, and cleanup
- Guarded local merge trains with per-candidate gates, exact failure reports,
  cancellation/resume, and all-or-nothing target promotion
- Bash, Zsh, and Fish completion plus an offline local workflow example and
  workflow-to-integration acceptance coverage

### Changed

- Hydra, native core, and native TUI release handshakes now report 1.9.0; protocol
  versions remain unchanged
- Completed candidate groups and workflow runs can feed the same verified integration
  path; no integration or workflow operation pushes a branch

### Security

- Repository workflow execution is bound to the existing `.hydra` trust hash
- Shell-string workflow commands require explicit `allow_shell: true`; argv remains
  the default
- Promotion revalidates the immutable manifest, candidates, verified result,
  approval, clean worktree, and target ref under a project lock

## [1.8.0] - 2026-08-30

### Added

- Optional C99 `hydra-tui` mission control with list, detail, coordination, recovery,
  search, sanitized pane preview, resize handling, and explicit source/confidence
  rendering
- Distinct declared, observed, liveness, stale, and unavailable states plus event,
  signal, message, gate, claim, scope, queue, resource, diff, and approval summaries
- Searchable fixed-action palette whose spawn, switch, kill, and regenerate actions
  delegate to the shell CLI with argv-safe process execution
- Recovery board for malformed state, stale locks, dead sessions, orphan worktrees,
  interrupted transitions, and teardown failures
- `hydra tui --native`, `--basic`, capability diagnostics, and a deterministic
  headless fixture renderer with explicit frame and terminal-size bounds
- Checksummed platform-qualified native TUI packaging, offline/source installation,
  sanitizer coverage, and Linux/macOS hosted build/test configuration
- Native/basic behavior, fallback, keymap, accessibility, terminal safety, and
  recovery documentation

### Changed

- The basic shell TUI remains the default for 1.8.0; native dispatch is opt-in
- Native observation uses measured bounded polling because the control-mode prototype
  did not yet qualify reconnect and flow-control reliability
- Hydra/core/TUI release handshakes now report 1.8.0 while protocol v1 remains stable

### Security

- Pane and fixture bytes are treated as untrusted and cannot emit terminal controls
  or feed the action parser
- Bracketed paste, unknown escape sequences, terminal dimensions, record counts, and
  preview capture are bounded
- Native code performs no state mutation or notification delivery

## [1.7.0] - 2026-08-30

### Added

- Expiring path claims, injected read/write scopes, four-category collision analysis,
  locked per-head resource allocation, and evidence-bearing verification gates with
  separate human approval
- Explicit typed context packs and guarded `sync`/`land` integration with merge
  simulation, pre-operation archives, dry runs, exact gate binding, teardown, and
  clean recovery from conflicts
- Per-head `du`, policy-driven `gc`, and worktree doctor lock, unlock, move, repair,
  and prune operations that preserve dirty/untracked work by default
- Optional read-only C99 `hydra-core` protocol v1 for capabilities, state/event
  validation, canonical JSON strings, and snapshot aggregation
- Strict native build, unit, parity, sanitizer, benchmark, packaging, and macOS/Linux
  CI lanes, including absent, skewed, crashing, timing-out, malformed, and
  non-executable helper fixtures
- Offline/source native installation with SHA-256, platform, dependency, exact
  version/protocol verification, atomic replacement, and rollback preservation
- Reproducible tmux control-mode prototype and published parallel-safety, native
  architecture/distribution, and versioning guidance

### Changed

- Shell remains the only mutation authority; native snapshot dispatch is explicit
  with `hydra snapshot --native` and deterministically falls back to canonical shell
- Completions and CLI help cover all 1.7 coordination, integration, disk, worktree,
  and snapshot commands

### Security

- Context packs include only selected typed inputs, hash file manifests instead of
  copying file contents, and reject likely secret-bearing paths
- Gate approval is invalidated by any commit or worktree-status change
- Resource, claim, archive, and teardown records remain project-scoped and locked

## [1.6.0] - 2026-08-29

### Added

- Project-, head-, and instance-scoped state v2 with reversible migration, backups,
  verification, opaque IDs, cross-project isolation, and versioned JSONL events
- Independent declared outcome, observed status, and liveness channels; explicit
  completion policies; durable waits; stale-instance rejection; resume history
- Agent profiles, capability reporting, trusted project initialization, task-aware
  spawn/dry-run/no-agent paths, and safe task-file injection
- Provider-neutral adapter ingest with bounded canonical JSON v1 validation and a
  dated capability matrix that makes absent adapters explicit
- Typed inbox/safe-point messages and receipts, rate-limited local lifecycle
  notifications, bounded transcripts, and trusted teardown hooks
- `hydra exec` with bounded parallelism, timeouts, private captured results, argv-safe
  execution, and separately authorized trusted shell-string mode
- Recorded-base `hydra diff`, `hydra review`, `hydra list --git`, and per-head
  `hydra provenance` views
- Versioned JSON success/error envelopes across automation-relevant `--json` commands
- State, lifecycle, adapter, automation, security, operations, provenance, and release
  evidence documentation

### Changed

- Dependencies require named durable evidence instead of treating a missing tmux
  session as success
- Repository configuration and hooks execute only while their exact content hash is
  trusted; a change invalidates trust
- Regenerate and resume create a new instance and preserve prior instance history
- State and message mutations use evidence-bearing shared locks and atomic replacement

### Fixed

- Timeout watchdogs now terminate the command process tree without retaining output
  pipes for the full timeout
- Shell integration fixtures isolate `HYDRA_HOME`, tmux state, branches, and worktrees
  and clean only resources they own
- State cache keys, sender identity, worktree paths with spaces, optional PR input,
  and tmux `:0.0` pane targeting are handled literally and reliably

## [1.5.2] - 2026-08-26

### Added
- Non-root `PREFIX` install: `$PREFIX/bin/hydra` and `$PREFIX/lib/hydra` (`install.sh` and `make install`)
- Matching `uninstall.sh` / `make uninstall` for the same prefix; optional `DESTDIR` staging
- Install verification runs `hydra version` and prints exact binary and library paths
- `hydra doctor` reports install layout, git/tmux versions, writable `HYDRA_HOME`, repo/worktree readiness, detected agents, and a `Next:` recovery for every failure
- Fresh-prefix `make test-install` and throwaway-repo `make smoke-onboarding` tests
- README five-minute Quick Start (run-from-source or prefix install, `HYDRA_SKIP_AI=1`, completions)

### Changed
- Library discovery: `HYDRA_ROOT`, source `../lib`, PREFIX `../lib/hydra`, then legacy `/usr/local/lib/hydra`
- First-run errors name the failed precondition and the next command or doc action
- Spawn fails if the selected agent is not on `PATH` unless `HYDRA_SKIP_AI=1`

### Fixed
- Installer and Make no longer require root or hard-code `/usr/local` as the only layout

## [1.5.1] - 2026-08-25

### Added
- Public contract snapshot in `docs/CONTRACTS.md` (map fields, JSON, locks, TUI rows, send-keys targets)
- Supported-platform statement (Linux and macOS, POSIX `sh`, `tmux >= 3.0`)
- `make bench` / `scripts/bench.sh` for list, status, doctor, and TUI refresh timings at 5 and 20 heads
- `tests/bin/tmux` stub for deterministic send-keys and pane-target tests
- Broadcast `--pane` and `--force` for explicit pane targeting

### Changed
- State map writers share the `state_map` mkdir lock and fail closed if it cannot be acquired
- Replacement files are created next to their destination before `mv`
- Queue filenames sort by priority (high first) then request time, with a monotonic seq for FIFO
- `json_escape` emits `\n` / `\r` / `\b` / `\f` and `\u00XX` for remaining C0 controls, and passes UTF-8 bytes through unchanged
- Startup and agent launch always send keys to `session:0.0`
- TUI list readers consume the eight-field tab row contract
- TUI activity prefers `window_activity` from a batched pane snapshot; capture-pane hashing is fallback
- `hydra list` / `status` use a tmux session snapshot instead of per-head `has-session` where loaded
- Shell completion covers every command dispatched by `bin/hydra`

### Fixed
- `hydra status` no longer treats deps/PR as part of the duration timestamp (`Illegal number`)
- Kill preflights dirty/untracked worktrees before tearing down tmux or mappings
- `hydra spawn` rolls back the session and worktree if the state map cannot be written
- `hydra kill` acquires the state-map lock before destroying tmux, and does not delete the worktree if mapping removal cannot be committed
- Send-keys and spawn live-probe tmux instead of a stale TUI/list snapshot
- `hydra broadcast` only auto-selects recognized shell panes; non-shell panes require `--pane` or `--force`
- `hydra broadcast` skips a live agent on `:0.0` (even when the map stores `-`) and may use `:0.0` again after that process exits
- Session-qualified `broadcast --pane session:target` applies only to that session
- `make bench` runs spawn/kill from a throwaway repo and does not sweep unrelated `bench-*` tmux sessions
- Message inboxes are removed from kill and dead-mapping cleanup
- Doctor/cleanup stale-lock detection matches empty `*.lock` directories
- `hydra regenerate` preserves group, deps, PR, and timestamp
- Malformed `.gitignore` entry; runtime `.hydra/` is no longer tracked

## [1.5.0] - 2026-08-24

### Added
- **TUI redesign**: Two-panel detail sidebar on wide terminals, context footer, spawn wizard
- **TUI key aliases**: `Enter` switches (same as `s`), `A` select-all, `D` dashboard, `f` preview follow
- **TUI config**: `HYDRA_TUI_REFRESH_MS`, `HYDRA_TUI_ACTIVITY_INTERVAL`, wired `HYDRA_TUI_PREVIEW_LINES`
- **Refactor**: Split god files into modules (`lib/cmd_*.sh`, `lib/tui_*.sh`, `lib/state_cache.sh`, `lib/maintenance.sh`)
- **Worktree helpers**: `list_hydra_worktrees`, `branch_from_hydra_worktree_path` in `lib/paths.sh`

### Changed
- **TUI**: Expanded search filters session name, group, and AI tool
- **TUI**: Activity detection uses cached pane hashes to reduce tmux load
- **TUI**: Terminal resize handling; honors `HYDRA_NONINTERACTIVE` for kill confirmations
- **Architecture**: `bin/hydra` slimmed to dispatcher (~470 lines); commands in `lib/cmd_*.sh`

### Fixed
- **Doctor/cleanup**: Orphan worktree detection uses `hydra-*` prefix (not repo basename)
- **Regenerate**: Slash-containing branch names (e.g. `feature/test-1`) discovered correctly
- **Spawn**: Rollback session/worktree/mapping when AI tool validation fails
- **Queue**: Mixed `--agents` spawn queue entries use correct branch names and AI tools
- **Queue**: Failed queue processing retains entry for retry
- **Limits**: Active session count excludes dead mappings
- **Limits**: Queue JSON output uses `json_escape`
- **Messages**: Lock retry instead of unsafe fallback write on contention
- **Switch**: fzf uses tab-separated fields; empty selection handled safely
- **Dashboard**: Configurable pane join retries (`HYDRA_DASHBOARD_JOIN_RETRIES`)
- **TUI**: Bulk group assignment calls `set_group`

## [1.4.2] - 2025-12-30

### Added
- **Doctor**: Auto-fix mode with `hydra doctor --fix`
  - Automatically runs `regenerate` for dead sessions
  - Automatically runs `cleanup` for stale locks and dead mappings
  - Orphaned worktrees require manual confirmation for safety
- **Tests**: Added comprehensive test coverage for template.sh (41 tests)
  - Covers all 11 template functions: init, list, get, exists, show, validate, create, delete, get_field, expand_vars, apply
- **Tests**: Added comprehensive test coverage for hooks.sh (16 tests)
  - Covers locate_config_dir, run_hook, apply_custom_layout_or_default, run_startup_commands

### Changed
- **Code Quality**: Refactored nested function in deps.sh to module scope
  - `_check_circular_recursive()` moved to `_check_circular_helper()` at module level
  - Uses module-level `_DEPS_VISITED` variable instead of closure semantics
  - Improves POSIX shell clarity and maintainability

## [1.4.1] - 2025-12-29

### Added
- **TUI**: Spawn with inline options support
  - Enter `my-branch --ai codex --template dev` when spawning
  - Supports `--ai`, `--template`/`-t`, `--layout`/`-l` flags
  - Shows parsed options before spawning

### Fixed
- **TUI**: Fixed incorrect "(current)" session detection when running outside tmux
  - `tmux display-message` could return stale session names from server state
  - Now checks `$TMUX` env var to confirm actually inside tmux
- **TUI**: Fixed spawn from TUI not attaching when outside tmux
  - After spawning a session via TUI, now automatically attaches to it
- **TUI**: Fixed switch from TUI not working when outside tmux
  - `switch-client` only works inside tmux; now uses `attach-session` when outside
- **Dashboard**: Fixed pane collection failing with non-zero tmux base-index
  - Removed hardcoded `:0` window references that assumed base-index=0

## [1.4.0] - 2025-12-28

### Added

#### Multi-Agent Workflows
- Session Dependencies for staged workflows
  - `hydra spawn feature-tests --after feature-impl` waits for dependencies
  - `hydra list --deps` shows dependency tree visualization
  - Circular dependency detection prevents infinite waits
  - Configurable timeout and polling intervals
- Resource Limits to prevent system overload
  - `HYDRA_MAX_SESSIONS` environment variable sets global session limit
  - Priority queue for pending spawns when limit is reached
  - `hydra queue` command to view/manage pending spawns
  - Queue automatically processed when sessions are killed
- Environment Setup Automation
  - `setup:` section in `.hydra/config.yml` runs commands before session creation
  - Commands execute blocking in worktree directory
  - `HYDRA_SKIP_SETUP=1` bypasses setup; `HYDRA_SETUP_CONTINUE=1` continues on failure
- Cross-Session Messaging for loose agent coordination
  - `hydra send <branch> "<message>"` queues message to session's inbox
  - `hydra recv [--peek] [--json]` reads messages for current session
  - File-based message queue at `~/.hydra/messages/`

#### GitHub Integration
- PR Integration
  - `hydra spawn --pr <#>` creates a head from an existing GitHub PR
  - `hydra spawn --pr-new` creates a draft PR after spawning
  - `hydra pr [<branch>]` creates or shows PR for a session
  - PR numbers stored in state file and displayed in `list`
- PR Status Display
  - `hydra list` shows `[PR #42 OPEN]` with status (open/merged/closed)
  - Cached status with configurable TTL (`HYDRA_PR_CACHE_TTL`, default 5 min)
  - `--no-pr-status` skips lookup; `--refresh-pr-status` forces refresh
  - JSON output includes `pr_status` field

#### Session Management
- Session Templates for reusable configurations
  - `hydra template list|create|show|delete|edit` commands
  - `hydra spawn <branch> --template <name>` applies template on spawn
  - Templates stored in `~/.hydra/templates/`
  - Variable expansion: `${BRANCH}`, `${SESSION}`, `${WORKTREE}`, `${REPO_ROOT}`
  - Merges with session-level `.hydra/config.yml` overrides
- Enhanced Group Workflows
  - `hydra group create <name> <branch> [branch...]` bulk creates groups
  - `hydra group wait <name>` blocks until all sessions in group are killed
  - `hydra group status <name> [--json]` shows group health

#### TUI Enhancements
- Session Preview panel
  - Press `p` to toggle preview panel showing session output
  - Displays last N lines (configurable via `HYDRA_TUI_PREVIEW_LINES`)
  - Auto-hides on small terminals

#### Testing
- New test suites: `test_kill.sh` (12 tests), `test_deps.sh` (15 tests)

### Fixed
- **Critical**: Undefined `require` function broke `--pr`, `--issue`, and `--after` spawn options
- **Bug**: Missing numeric validation in limits.sh could cause silent failures
- **Test**: `test_layout.sh` now properly tests with `HYDRA_DISABLE_HOTKEYS`
- **Test**: `test_json_output.sh` improved JSON structure validation

### Changed
- State file format extended to 7 fields (backward compatible):
  `branch session ai group timestamp deps pr`
- Kill command now loads limits library for queue processing
- Performance: Added tmux session caching to reduce subprocess calls

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
