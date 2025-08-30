# Project TODOs

## Planning Session Output
Date: 2025-01-03

### Migration Rationale
- Hydra shell implementation hitting limitations with POSIX compliance
- Complex features becoming difficult to maintain in shell
- Performance constraints with shell scripting
- Need for better testing and type safety

### Technical Decisions
- Architecture: Go with modular package design mirroring current lib/*.sh structure
- CLI Framework: Cobra for robust subcommand handling and automatic help generation
- Compatibility: Maintain all existing commands, flags, and file formats
- Key Constraints: Must maintain feature parity, tmux ≥3.0 requirement, ~/.hydra/map compatibility

### Initial Task Breakdown
Migration organized into 5 phases:
1. Project foundation and scaffolding
2. Core logic implementation (git, tmux, state, commands)
3. Comprehensive testing
4. Build system and CI/CD updates
5. Documentation updates

## In Progress

- [ ] Review Gemini Pro's migration plan feedback and update TODO.md accordingly
  - Context: Expert analysis identified critical improvements needed
  - Status: Incorporating feedback into plan structure

## Priority Spikes (Do First)

### Critical Architecture Decisions

- [ ] **SPIKE 1**: go-git vs os/exec Decision (1-3 days)
  - Context: Fundamental architectural choice that affects entire implementation
  - Acceptance Criteria:
    - Test go-git worktree operations (add, remove, list)
    - Measure memory usage on 100k commit repo
    - Verify correctness of status operations
    - Document findings and recommendation
  - Research Results: go-git has 8x memory usage, incomplete worktree support
  - **Recommendation**: Use git CLI adapter pattern

- [ ] **SPIKE 2**: fzf PTY Integration Proof-of-Concept (2-4 days)
  - Context: Most complex user interaction requiring PTY handling
  - Acceptance Criteria:
    - Create minimal Go program that pipes to fzf
    - Handle selection and cancellation
    - Test with github.com/creack/pty
    - Implement timeout handling
    - Document PTY interaction patterns
  - Dependencies: Can run parallel with SPIKE 1

- [ ] **SPIKE 3**: Define Minimum Viable Migration (1 day)
  - Context: Focus on core features first for faster feedback
  - Core Commands for MVM:
    - `hydra list` - List worktrees and sessions
    - `hydra spawn <branch>` - Create worktree and session
    - `hydra switch` - Interactive session switching
    - `hydra kill <branch>` - Clean up worktree and session
  - Acceptance: Document what's in/out of MVM scope

## Backlog

### Phase 1: Foundation (Priority: Critical)

- [ ] 1.1 Create Go Module Structure
  - Context: Initialize Go project with proper module path
  - Acceptance Criteria: 
    - go.mod created with github.com/Someblueman/hydra path
    - Basic directory structure established
    - Can run `go build` successfully
  - Technical Notes: Consider using hydra-go/ subdirectory during development
  - Dependencies: None
  - Delegation: Architecture subagent for optimal package structure

- [ ] 1.2 Integrate Cobra CLI Framework
  - Context: Replace manual argv parsing with robust CLI library
  - Acceptance Criteria:
    - Cobra dependency added
    - Root command configured with name, description, version
    - Basic --help and --version flags working
  - Technical Notes: Cobra's pattern maps well to Hydra's subcommands
  - Dependencies: 1.1 must be complete

- [ ] 1.3 Establish Internal Package Structure
  - Context: Mirror shell's modular design (lib/*.sh) in Go packages
  - Acceptance Criteria:
    - internal/git/ package created
    - internal/tmux/ package created  
    - internal/state/ package created
    - internal/layout/ package created (optional)
    - internal/dashboard/ package created (optional)
  - Technical Notes: Use internal/ to prevent external imports
  - Dependencies: 1.1 must be complete
  - Delegation: Architecture subagent for package interfaces

- [ ] 1.4 Port Hydra Home Initialization
  - Context: Ensure ~/.hydra directory and map file creation on startup
  - Acceptance Criteria:
    - InitHydraHome() function implemented
    - Respects HYDRA_HOME environment variable
    - Creates ~/.hydra/map if not exists
    - Called from main() or Cobra PersistentPreRun
  - Technical Notes: Use os.UserHomeDir() for home directory
  - Dependencies: 1.3 must be complete

- [ ] 1.5 Define Error Handling and Logging Strategy
  - Context: Establish consistent error handling patterns before implementation
  - Acceptance Criteria:
    - Error wrapping strategy documented
    - Custom error types defined if needed
    - Logging approach selected (consider slog for structured logging)
    - Error context propagation pattern established
  - Priority: Critical
  - Dependencies: 1.1 must be complete

### Phase 2: Core Implementation (Priority: High)

- [ ] 2.1 Implement Tmux Package
  - Context: Port all tmux-related operations from lib/tmux.sh
  - Acceptance Criteria: All functions below implemented and tested
  - Dependencies: Phase 1 complete
  - Delegation: Shell expert subagent for complex tmux operations
  - Subtasks:
    - [ ] 2.1.1 CheckTmuxVersion() - Verify tmux ≥3.0
    - [ ] 2.1.2 SessionExists(name string) bool
    - [ ] 2.1.3 CreateSession(name, dir string) error
    - [ ] 2.1.4 KillSession(name string) error
    - [ ] 2.1.5 ListSessions() ([]string, error)
    - [ ] 2.1.6 SendKeys(session, keys string) error
    - [ ] 2.1.7 SwitchToSession(name string) error
    - [ ] 2.1.8 RenameSession(old, new string) error
    - [ ] 2.1.9 Unit tests for tmux package (TDD approach)
      - Test command construction and error handling
      - Mock external tmux calls for isolated testing
      - Test version detection and parsing

- [ ] 2.2 Implement Git Package
  - Context: Port git worktree operations from lib/git.sh
  - Acceptance Criteria: All functions below implemented with security validations
  - Dependencies: Phase 1 complete
  - Delegation: Security subagent for validation functions
  - Subtasks:
    - [ ] 2.2.1 BranchExists(branch string) bool
    - [ ] 2.2.2 ValidateBranchName(name string) bool (SECURITY CRITICAL)
    - [ ] 2.2.3 CreateWorktree(branch, path string) error
    - [ ] 2.2.4 DeleteWorktree(path string, force bool) error
    - [ ] 2.2.5 FindWorktreePath(branch string) (string, bool)
    - [ ] 2.2.6 FindWorktreeByPattern(pattern string) (string, bool)
    - [ ] 2.2.7 GetRepoRoot() (string, error)
    - [ ] 2.2.8 Unit tests for git package (TDD approach)
      - Focus on validation functions and security
      - Test error cases and edge conditions
      - Mock git command outputs

- [ ] 2.3 Implement State Management Package
  - Context: Manage ~/.hydra/map file for branch-session mappings
  - Acceptance Criteria: Maintains compatibility with existing map format
  - Dependencies: Phase 1 complete
  - Subtasks:
    - [ ] 2.3.1 AddMapping(branch, session string) error
    - [ ] 2.3.2 RemoveMapping(branch string) error
    - [ ] 2.3.3 GetSessionForBranch(branch string) (string, bool)
    - [ ] 2.3.4 GetBranchForSession(session string) (string, bool)
    - [ ] 2.3.5 ListMappings() ([]Mapping, error)
    - [ ] 2.3.6 ValidateMappings() []string (warnings)
    - [ ] 2.3.7 CleanupMappings() error
    - [ ] 2.3.8 GenerateSessionName(branch string) string
    - [ ] 2.3.9 Unit tests for state package (TDD approach)
      - Test file I/O and mapping logic
      - Use temporary files for isolation
      - Test concurrent access handling
    - [ ] 2.3.10 Implement atomic file writes
      - Acceptance: Writes to temp file first
      - Acceptance: Uses atomic rename operation
      - Acceptance: Handles write failures without corruption
      - Technical: Use ioutil.TempFile + os.Rename
    - [ ] 2.3.11 Implement format decision
      - Acceptance: Documents choice of plaintext vs JSON
      - Acceptance: Plaintext = "branch session" per line
      - Acceptance: Handles parsing edge cases
    - [ ] 2.3.12 Add file locking (optional)
      - Acceptance: Prevents concurrent modifications
      - Acceptance: Uses advisory locks if available
      - Acceptance: Falls back gracefully if not supported

- [ ] 2.4 Implement Layout Package (Optional)
  - Context: Port tmux pane layout management from lib/layout.sh
  - Acceptance Criteria: Can apply and cycle through layouts
  - Dependencies: 2.1 must be complete
  - Priority: Lower
  - Subtasks:
    - [ ] 2.4.1 ApplyLayout(session, layoutName string) error
    - [ ] 2.4.2 CycleLayout(session string) error
    - [ ] 2.4.3 GetCurrentLayout(session string) (string, error)

- [ ] 2.5 Implement Dashboard Package (Optional)
  - Context: Port multi-session dashboard view from lib/dashboard.sh
  - Acceptance Criteria: Can create/destroy dashboard with pane collection
  - Dependencies: 2.1, 2.3 must be complete
  - Priority: Lowest
  - Subtasks:
    - [ ] 2.5.1 CreateDashboardSession() error
    - [ ] 2.5.2 CollectSessionPanes() error
    - [ ] 2.5.3 RestorePanes() error
    - [ ] 2.5.4 ArrangeDashboardLayout() error

- [ ] 2.6 Implement GitHub Integration Package
  - Context: Enable spawning sessions directly from GitHub issues via --issue flag
  - Acceptance Criteria:
    - Can fetch issue details from GitHub using gh CLI
    - Generates valid branch names from issue titles
    - Handles closed issues with user confirmation
    - Integrates seamlessly with spawn command
  - Dependencies: 2.1 (tmux), 2.2 (git), 2.3 (state) must be complete
  - Delegation: API design subagent for GitHub integration patterns
  - Subtasks:
    - [ ] 2.6.1 EnsureGhCLI() - Check gh CLI availability
      - Acceptance: Verifies gh is installed via exec.LookPath("gh")
      - Acceptance: Checks authentication via gh auth status
      - Acceptance: Returns clear error message if not available
    - [ ] 2.6.2 FetchIssueDetails(issueNum int) (title, state string, err error)
      - Acceptance: Executes gh issue view with JSON output
      - Acceptance: Parses JSON correctly for title and state
      - Acceptance: Handles API errors gracefully
      - Technical: Use encoding/json for parsing
    - [ ] 2.6.3 IssueBranchName(issueNum int, title string) string
      - Acceptance: Generates branch like "issue-123-feature-title"
      - Acceptance: Sanitizes special characters to hyphens
      - Acceptance: Limits title portion to 50 characters
      - Acceptance: Removes trailing hyphens
    - [ ] 2.6.4 PromptForClosedIssue() bool
      - Acceptance: Shows warning "Issue is closed, continue? [y/N]"
      - Acceptance: Reads user input from stdin
      - Acceptance: Returns false by default (N)
    - [ ] 2.6.5 Unit tests for GitHub package
      - Mock gh command outputs for testing
      - Test JSON parsing with various formats
      - Test branch name generation edge cases

- [ ] 2.7 Implement AI Tools Management Package
  - Context: Validate and manage AI tool commands with security restrictions
  - Acceptance Criteria:
    - Maintains allowlist of supported AI tools
    - Validates AI commands before execution
    - Handles default AI tool from environment
    - Supports bulk spawn and mixed agents
  - Dependencies: 2.1 (tmux), 2.3 (state) must be complete
  - Priority: High (core feature)
  - Subtasks:
    - [ ] 2.7.1 ValidateAICommand(cmd string) error
      - Acceptance: Only allows: claude, codex, cursor, copilot, aider, gemini
      - Acceptance: Returns descriptive error for unsupported commands
      - Acceptance: Case-sensitive matching
    - [ ] 2.7.2 GetDefaultAITool() string
      - Acceptance: Reads HYDRA_AI_COMMAND env variable
      - Acceptance: Falls back to "claude" if not set
      - Acceptance: Validates the default tool
    - [ ] 2.7.3 ParseAgentsSpec(spec string) ([]AgentSpec, error)
      - Acceptance: Parses "claude:2,aider:1" format
      - Acceptance: Validates each agent name
      - Acceptance: Validates counts are positive integers
      - Acceptance: Returns error for malformed specs
    - [ ] 2.7.4 Unit tests for AI package
      - Test validation with all allowed/disallowed commands
      - Test agent spec parsing with various inputs
      - Test environment variable handling

- [ ] 2.8 Implement Cobra Commands
  - Context: Create command handlers using internal packages
  - Dependencies: 2.1, 2.2, 2.3, 2.6, 2.7 must be complete
  - Subtasks:
    - [ ] 2.8.1 spawn command with all flags
      - Acceptance Criteria:
        - Handles branch name already exists
        - Handles worktree path conflicts
        - Handles tmux server not running
        - Handles git repository not found
        - Handles invalid branch names (security)
        - Handles AI command validation
        - Handles TTY vs non-TTY environments
        - Handles --issue flag for GitHub integration
        - Handles --count/-n for bulk spawn
        - Handles --agents for mixed AI tools
        - Validates mutual exclusivity of flags
        - Implements rollback on failure
        - Auto-attaches in interactive mode for bulk spawn
      - Technical: Must validate AI command, attach if TTY
      - Subtasks:
        - [ ] 2.8.1.1 Implement GitHub issue spawn flow
          - Acceptance: Checks --issue and branch are mutually exclusive
          - Acceptance: Validates --issue incompatible with -n/--agents
          - Acceptance: Calls GitHub package to fetch and validate issue
          - Acceptance: Handles closed issue confirmation
          - Acceptance: Proceeds with generated branch name
        - [ ] 2.8.1.2 Implement bulk spawn (-n flag)
          - Acceptance: Creates N sessions with branch-1, branch-2 naming
          - Acceptance: Prompts confirmation if N > 3
          - Acceptance: Tracks success/failure count
          - Acceptance: On failure, prompts to continue or rollback
          - Acceptance: Rollback calls kill on all created sessions
          - Acceptance: Prints summary "Succeeded: X, Failed: Y"
          - Acceptance: Auto-switches to first session if interactive
        - [ ] 2.8.1.3 Implement mixed agents (--agents flag)
          - Acceptance: Parses spec like "claude:2,aider:1"
          - Acceptance: Validates total > 3 requires confirmation
          - Acceptance: Creates sessions with incremental numbering
          - Acceptance: Assigns correct AI tool to each session
          - Acceptance: Same rollback behavior as bulk spawn
        - [ ] 2.8.1.4 Implement worktree rollback
          - Acceptance: If tmux creation fails, deletes worktree
          - Acceptance: Uses DeleteWorktree(path, force=false)
          - Acceptance: Logs rollback action
    - [ ] 2.8.2 list command
      - Acceptance: Shows branch/session/status/path in columns
    - [ ] 2.8.3 switch command with interactive selection
      - Context: User-friendly session switching with fzf
      - Acceptance Criteria:
        - Detects and uses fzf if available
        - Falls back to numeric selection
        - Handles PTY for fzf interaction
        - Handles cancellation gracefully
      - Dependencies: 2.1, 2.3 must be complete
      - Priority: High (UX critical)
      - Subtasks:
        - [ ] 2.8.3.1 Detect fzf availability
          - Acceptance: Uses exec.LookPath("fzf")
          - Acceptance: Sets flag for selection mode
        - [ ] 2.8.3.2 Implement fzf pipe logic with PTY handling
          - Acceptance: Creates PTY for fzf interaction
          - Acceptance: Pipes branch list to fzf stdin
          - Acceptance: Captures selected branch from stdout
          - Acceptance: Handles fzf exit codes (0=selected, 130=cancelled)
          - Technical: Use github.com/creack/pty or similar
        - [ ] 2.8.3.3 Implement numeric fallback
          - Acceptance: Lists branches with numbers
          - Acceptance: Prompts "Enter number: "
          - Acceptance: Validates input is valid number
          - Acceptance: Handles out-of-range selections
        - [ ] 2.8.3.4 Handle selection and switching
          - Acceptance: Maps selection to session name
          - Acceptance: Calls tmux.SwitchToSession
          - Acceptance: Shows error if session not found
          - Acceptance: Exits cleanly on cancellation
    - [ ] 2.8.4 kill command with --all and --force flags
      - Context: Safely remove sessions with confirmation
      - Acceptance Criteria:
        - Implements --all flag for bulk deletion
        - Implements --force to skip confirmations
        - Handles interactive vs non-interactive environments
        - Cleans up stale mappings
        - Provides detailed success/failure reporting
      - Dependencies: 2.1, 2.2, 2.3 must be complete
      - Subtasks:
        - [ ] 2.8.4.1 Implement --all flag logic
          - Acceptance: Errors if both branch and --all provided
          - Acceptance: Lists all heads for confirmation
          - Acceptance: Shows "No active Hydra heads" if none
          - Acceptance: Prompts "Kill all X heads? [y/N]"
          - Acceptance: Refuses in non-interactive without --force
        - [ ] 2.8.4.2 Implement kill iteration logic
          - Acceptance: For each mapping, tries tmux.KillSession
          - Acceptance: Removes mapping even if session dead
          - Acceptance: Deletes worktree directory
          - Acceptance: Tracks success/failure count
          - Acceptance: Continues on individual failures
        - [ ] 2.8.4.3 Implement single kill flow
          - Acceptance: Looks up session from branch
          - Acceptance: Shows "No session found" if missing
          - Acceptance: Prompts confirmation unless --force
          - Acceptance: Handles killing current session edge case
        - [ ] 2.8.4.4 Implement HYDRA_NONINTERACTIVE support
          - Acceptance: Checks env var for non-interactive mode
          - Acceptance: Skips all prompts if set
          - Acceptance: Documents in help text
    - [ ] 2.8.5 regenerate command
      - Acceptance: Recreates sessions for existing worktrees
    - [ ] 2.8.6 status command
      - Acceptance Criteria:
        - Displays current git branch
        - Shows hydra session name
        - Shows tmux session status (attached/detached)
        - Shows worktree path
        - Shows session health indicators
    - [ ] 2.8.7 doctor command
      - Acceptance Criteria:
        - Verifies git is in PATH and version
        - Verifies tmux version is ≥3.0
        - Validates integrity of ~/.hydra/map file
        - Checks for orphaned worktrees
        - Checks for orphaned tmux sessions
        - Measures command dispatch time (<50ms)
        - Measures session switch time (<100ms)
        - Reports any performance anomalies
        - Measures actual command dispatch time
        - Measures actual session switch time
        - Compares against targets (<50ms, <100ms)
        - Identifies performance bottlenecks
      - Delegation: Performance subagent for optimization
      - Subtasks:
        - [ ] 2.8.7.1 Implement dispatch time measurement
          - Acceptance: Times execution of "hydra version"
          - Acceptance: Uses time.Now() for precision
          - Acceptance: Reports in milliseconds
          - Acceptance: Warns if >50ms
        - [ ] 2.8.7.2 Implement switch time measurement
          - Acceptance: Creates test session
          - Acceptance: Times switch operation
          - Acceptance: Cleans up test session
          - Acceptance: Warns if >100ms
        - [ ] 2.8.7.3 Implement bottleneck detection
          - Acceptance: Profiles slow operations
          - Acceptance: Suggests optimizations
          - Acceptance: Links to performance guide
    - [ ] 2.8.8 dashboard command (Complex - depends on 2.5)
      - Context: Aggregate all sessions into single view
      - Acceptance Criteria:
        - Creates hydra-dashboard session
        - Collects first pane from each session
        - Arranges panes based on count
        - Binds 'q' key for exit
        - Restores panes on exit
      - Dependencies: 2.1, 2.3, 2.5 must be complete
      - Priority: Low (advanced feature)
      - Delegation: Consider UI/UX subagent for layout logic
      - Subtasks:
        - [ ] 2.8.8.1 Implement dashboard creation
          - Acceptance: Creates new tmux session "hydra-dashboard"
          - Acceptance: Errors if dashboard already exists
        - [ ] 2.8.8.2 Implement pane collection
          - Acceptance: Gets pane info via tmux list-panes
          - Acceptance: Records pane_id, session, window
          - Acceptance: Renames pane title to branch name
          - Acceptance: Moves pane to dashboard via join-pane
        - [ ] 2.8.8.3 Implement layout arrangement
          - Acceptance: 1 pane = default layout
          - Acceptance: 2 panes = even-horizontal
          - Acceptance: 3 panes = main-horizontal
          - Acceptance: 4+ panes = tiled
        - [ ] 2.8.8.4 Implement exit binding
          - Acceptance: Binds 'q' to 'hydra dashboard-exit'
          - Acceptance: Saves restore info to temp file
        - [ ] 2.8.8.5 Implement dashboard-exit command
          - Acceptance: Hidden Cobra command
          - Acceptance: Reads restore info from temp file
          - Acceptance: Restores each pane to original session
          - Acceptance: Kills dashboard session
          - Acceptance: Cleans up temp file
    - [ ] 2.8.9 completion command for shell completions
    - [ ] 2.8.10 version command

### Phase 2.9: Security Implementation (Priority: Critical)

- [ ] 2.9 Implement Security Validations
  - Context: Prevent command injection and path traversal attacks
  - Acceptance Criteria:
    - All user inputs validated before use
    - Command construction prevents injection
    - Path operations prevent traversal
    - Clear error messages for invalid inputs
  - Dependencies: None (can start immediately)
  - Delegation: Security subagent for review
  - Subtasks:
    - [ ] 2.9.1 Implement safe command execution patterns
      - Acceptance: Never use shell expansion
      - Acceptance: Always use exec.Command with args array
      - Acceptance: Use "--" separator for git commands
      - Technical: Document pattern in contributing guide
    - [ ] 2.9.2 Enhance ValidateBranchName (in 2.2.2)
      - Acceptance: Rejects empty strings
      - Acceptance: Rejects names starting with "-"
      - Acceptance: Rejects shell metacharacters: ; | & $ ( ) { } [ ] < > * ' " ` \
      - Acceptance: Rejects ".." sequences
      - Acceptance: Limits length to 255 characters
    - [ ] 2.9.3 Implement ValidateWorkteePath
      - Acceptance: Rejects empty paths
      - Acceptance: Rejects ".." sequences
      - Acceptance: Rejects absolute paths to system directories
      - Acceptance: Validates parent directory exists
    - [ ] 2.9.4 Security unit tests
      - Test all validation functions with malicious inputs
      - Test command construction with injection attempts
      - Document security assumptions

### Phase 3: Testing (Priority: High)

- [ ] 3.1 Design Test Strategy
  - Context: Need comprehensive test coverage for Go implementation
  - Acceptance Criteria: 
    - Test patterns established
    - Mock/stub strategy defined
    - Integration test approach documented
    - Mock strategy for external commands defined
    - GitHub integration testing approach documented
    - Interactive command testing patterns established
    - Performance testing methodology defined
  - Dependencies: Phase 2 core packages complete
  - Delegation: Testing expert subagent
  - Subtasks:
    - [ ] 3.1.1 Define command mocking strategy
      - Acceptance: Interface for command execution
      - Acceptance: Mock implementation for tests
      - Acceptance: Real implementation for production
    - [ ] 3.1.2 Define GitHub testing approach
      - Acceptance: Mock gh CLI responses
      - Acceptance: Test data for various scenarios
      - Acceptance: Integration test markers
    - [ ] 3.1.3 Define interactive testing
      - Acceptance: PTY testing for fzf
      - Acceptance: Stdin simulation for prompts
      - Acceptance: Timeout handling for hangs
    - [ ] 3.1.4 Define performance testing
      - Acceptance: Benchmark key operations
      - Acceptance: Assert performance targets
      - Acceptance: Profile for bottlenecks

- [ ] 3.2 Unit Tests by Package
  - Context: Test individual package functions in isolation
  - Dependencies: Corresponding package implemented
  - Subtasks:
    - [ ] 3.2.1 git package unit tests
      - Focus: Validation functions, error cases
    - [ ] 3.2.2 tmux package unit tests  
      - Focus: Command construction, error handling
    - [ ] 3.2.3 state package unit tests
      - Focus: File I/O, mapping logic
    - [ ] 3.2.4 Validation function security tests

- [ ] 3.3 Integration Tests
  - Context: End-to-end testing of commands
  - Acceptance Criteria: 
    - All commands tested with real git/tmux
    - Edge cases covered
    - Error messages match shell version
  - Dependencies: All commands implemented
  - Subtasks:
    - [ ] 3.3.1 Spawn/kill/list workflow tests
    - [ ] 3.3.2 Switch command with/without fzf
    - [ ] 3.3.3 Regenerate scenarios
    - [ ] 3.3.4 Error handling and validation tests

- [ ] 3.4 Compatibility Testing
  - Context: Ensure seamless transition between shell and Go versions
  - Acceptance Criteria:
    - Go binary reads shell-created ~/.hydra/map correctly
    - Shell version reads Go-created ~/.hydra/map correctly
    - All edge cases in map format handled
    - Migration script tested if needed
    - Handles branch names with spaces (edge case)
    - Handles empty map files
    - Handles corrupt map entries gracefully
  - Priority: Critical
  - Dependencies: 2.3 (State Management) complete
  - Subtasks:
    - [ ] 3.4.1 Test map file format compatibility
      - Create map with shell version, read with Go
      - Create map with Go version, read with shell
      - Test edge cases (spaces, special chars)
    - [ ] 3.4.2 Test worktree compatibility
      - Ensure Go can manage shell-created worktrees
      - Ensure shell can manage Go-created worktrees
    - [ ] 3.4.3 Test session naming compatibility
      - Verify same session names generated
      - Test collision handling matches

### Phase 4: Build & CI/CD (Priority: Medium)

- [ ] 4.1 Update Makefile
  - Context: Replace shell-specific targets with Go equivalents
  - Acceptance Criteria:
    - `make build` produces binary
    - `make test` runs Go tests
    - `make install` installs binary to /usr/local/bin
    - Shell-specific targets removed
  - Dependencies: Phase 3 tests passing

- [ ] 4.2 Update CI Pipeline
  - Context: GitHub Actions needs Go instead of shell tools
  - Acceptance Criteria:
    - ShellCheck steps removed
    - Go build/test steps added
    - Linux/macOS matrix maintained
    - tmux installed for integration tests
  - Dependencies: 4.1 complete
  - Delegation: CI/CD expert subagent

- [ ] 4.3 Update Install/Uninstall Scripts
  - Context: Scripts need to handle binary instead of shell files
  - Acceptance Criteria:
    - install.sh builds and copies binary
    - uninstall.sh removes binary
    - Old shell library paths cleaned up
  - Dependencies: 4.1 complete

### Phase 4.5: User Acceptance & Migration (Priority: High)

- [ ] 4.5.1 Pre-release Alpha/Beta Testing
  - Context: Real users find issues automated tests miss
  - Acceptance Criteria:
    - Alpha builds distributed to 5-10 power users
    - Feedback collection process established
    - All reported issues triaged and resolved
    - Beta phase completed with broader user group
  - Priority: High
  - Dependencies: Phase 3 complete

- [ ] 4.5.2 Define User Migration Path
  - Context: Smooth upgrade experience critical for adoption
  - Acceptance Criteria:
    - Migration guide written
    - Upgrade script tested on multiple systems
    - Rollback procedure documented
    - FAQ for common issues created
  - Priority: High
  - Dependencies: 4.5.1 feedback incorporated

### Phase 5: Documentation (Priority: Medium)

- [ ] 5.1 Update README.md
  - Context: Remove POSIX references, update for Go
  - Acceptance Criteria:
    - POSIX badge removed
    - Installation instructions updated
    - Requirements updated (remove shell deps)
    - Usage examples verified still accurate
  - Dependencies: Phase 4 complete

- [ ] 5.2 Rewrite CONTRIBUTING.md
  - Context: Replace shell guidelines with Go
  - Acceptance Criteria:
    - Shell style guide removed
    - Go development guidelines added
    - Test instructions updated
    - No references to ShellCheck/dash
  - Dependencies: Phase 4 complete

- [ ] 5.3 Update CHANGELOG.md
  - Context: Document migration as major change
  - Acceptance Criteria:
    - Migration documented under new version
    - Breaking changes noted (install method)
    - Version bump decision made
  - Dependencies: All phases complete

## Risk Mitigation Strategies

### Critical Risks

1. **External Command Brittleness**
   - Use porcelain formats for git commands where available (--porcelain, --quiet)
   - Test with multiple git/tmux versions (minimum git 2.20, tmux 3.0)
   - Document all command assumptions and output format dependencies
   - **DECISION**: Avoid go-git due to worktree limitations and performance issues
   - Implement dedicated git adapter package for safe command execution

2. **Architectural Decision Lock-in**
   - **Priority 1 Spike**: go-git vs os/exec evaluation (1-3 days)
   - Research shows go-git has incomplete worktree support and 8x memory usage
   - Recommendation: Use git CLI adapter pattern for correctness guarantees
   - All git interaction through single internal/gitcmd package

3. **Environment Parity & Platform Differences**
   - Document breaking changes (no aliases, shell functions, or .bashrc vars)
   - Test across Linux distros, macOS versions, and shell environments
   - Consider GIT_CONFIG and PATH resolution differences
   - Explicitly scope OS support (Linux/macOS only, no Windows)
   - Implement pre-flight checks for dependency versions

4. **User Migration and Breaking Changes**
   - Configuration migration path needed (env vars, ~/.hydra_rc)
   - Create `hydra import-config` command for smooth transition
   - Document all behavioral divergences in release notes
   - Risk: Users may not upgrade if migration is painful
   - Measure: <5% rollback rate during beta

5. **fzf Integration Complexity**
   - **Priority 2 Spike**: Create PTY proof-of-concept (2-4 days)
   - Use proven PTY libraries (github.com/creack/pty)
   - Implement robust numeric fallback mechanism
   - Test cancellation scenarios and timeout handling
   - Define UX degradation metrics for fallback mode

6. **Security Considerations**
   - All os/exec usage reviewed with exec.CommandContext (no shell expansion)
   - Use "--" separator for all git commands to prevent injection
   - State file security: Validate ~/.hydra/map integrity
   - Implement file locking for concurrent access protection
   - Path traversal protection with strict validation

7. **Concurrency and State Management**
   - Risk: Race conditions on ~/.hydra/map file access
   - Implement atomic file writes (temp file + rename)
   - Consider advisory file locking where supported
   - Handle orphaned sessions and worktrees gracefully
   - Error propagation from external commands

8. **Dependency Version Drift**
   - AI tool APIs may change (claude, aider, etc.)
   - Git/tmux behavior changes in new versions
   - Implement version detection and compatibility warnings
   - Maintain compatibility matrix in documentation

## Success Metrics

### Core Functionality
- **Feature Coverage**: All core user workflows implemented and tested
  - Creating new session: `hydra spawn <branch>`
  - Switching sessions: `hydra switch` (with fzf)
  - Listing sessions: `hydra list`
  - Removing sessions: `hydra kill <branch>`
  - Documented list of any non-migrated features with justification

### Performance Targets
- **Interactive Commands**: 
  - `hydra list`: <100ms on 50 worktree repo
  - `hydra switch`: <100ms response time
  - `hydra spawn`: <500ms for new session creation
- **Dispatch Time**: <50ms for command dispatch (measured by `hydra doctor`)
- **Worst-case Latency**: 99th percentile <2x average under load

### Data Integrity
- **Migration Safety**: 
  - Zero corruption of ~/.hydra/map during concurrent access
  - No destructive git operations without explicit confirmation
  - Automated tests verify no data loss scenarios
  - Rollback procedure documented and tested
- **State Validation**: 
  - Orphan detection and cleanup mechanisms
  - Session-worktree synchronization verified

### User Experience
- **Beta Testing Metrics**:
  - <5% rollback rate to shell version
  - >95% workflow completion rate
  - Net Promoter Score >40
  - Qualitative feedback collected and addressed
- **Error Handling**:
  - Clear error messages for all failure modes
  - Graceful degradation when fzf unavailable
  - Pre-flight checks provide actionable diagnostics

### Security & Quality
- **Security Scanning**:
  - `govulncheck` integrated in CI with zero Critical/High vulnerabilities
  - All user inputs sanitized with documented validation rules
  - Command construction audit trail in code reviews
- **Test Coverage**:
  - >80% unit test coverage
  - Integration tests for all primary commands
  - Error path coverage for external command failures
  - Security regression test suite

### Platform Support
- **Compatibility Matrix**:
  - Linux (Ubuntu 20.04+, Fedora 35+, Arch)
  - macOS (11.0+)
  - Git 2.20+ and tmux 3.0+ verified
  - Shell compatibility: sh, dash, bash, zsh

### Migration Success
- **Adoption Metrics**:
  - Migration guide completion rate >90%
  - Support ticket volume <10% of user base
  - Time to migrate <10 minutes per user
  - Configuration import success rate >99%

## Completed