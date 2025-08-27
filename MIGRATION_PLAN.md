# Hydra Shell-to-Go Migration Plan

## Executive Summary

Migrating Hydra from POSIX shell to Go to overcome shell limitations and improve maintainability, performance, and testing capabilities.

## Key Architectural Decisions

### 1. Git Integration: CLI Adapter Pattern ✓

After evaluating go-git vs shelling out to git CLI:

**Decision**: Use git CLI via dedicated adapter package

**Rationale**:
- go-git has incomplete worktree support with known bugs
- go-git uses 8x more memory on large repos (5GB vs 290MB)
- git CLI guarantees 100% feature parity and correctness
- Porcelain formats (--porcelain, --quiet) make parsing reliable

**Implementation**: Create `internal/gitcmd` package as single point of git interaction

### 2. Priority Work Order

1. **Architecture Spikes** (Week 1)
   - SPIKE 1: Validate git adapter pattern (1-3 days)
   - SPIKE 2: fzf PTY integration POC (2-4 days)
   - SPIKE 3: Define Minimum Viable Migration (1 day)

2. **Minimum Viable Migration** (Weeks 2-3)
   - Core commands only: list, spawn, switch, kill
   - Integration test harness from day one
   - Defer advanced features (dashboard, bulk operations)

3. **Full Feature Implementation** (Weeks 4-6)
   - Add remaining commands
   - Performance optimization
   - Beta testing preparation

## Critical Success Factors

### 1. Data Safety
- Zero corruption of ~/.hydra/map file
- Atomic file operations with proper locking
- No destructive git operations without confirmation

### 2. User Experience
- <5% rollback rate during beta
- Migration takes <10 minutes per user
- Clear migration guide and import tools

### 3. Performance
- Command dispatch <50ms
- Interactive commands <100ms
- No performance regression vs shell

### 4. Quality
- >80% test coverage
- Zero critical security vulnerabilities
- All core workflows tested end-to-end

## Risk Matrix

| Risk | Impact | Mitigation |
|------|--------|------------|
| Git CLI changes | High | Use stable porcelain formats, test multiple versions |
| PTY complexity | High | Early spike, proven libraries, numeric fallback |
| User adoption | High | Beta program, migration tools, clear docs |
| Concurrency bugs | Medium | Atomic operations, integration tests |
| Platform differences | Medium | CI matrix testing, explicit OS support |

## Timeline

- Week 1: Architecture spikes and decisions
- Weeks 2-3: Build MVM with test harness
- Weeks 4-5: Complete feature implementation
- Week 6: Beta testing preparation
- Weeks 7-8: Beta program and fixes
- Week 9: General availability

## Definition of Done

- [ ] All spikes completed with documented decisions
- [ ] MVM implemented with full test coverage
- [ ] Performance targets met (verified by benchmarks)
- [ ] Security review completed
- [ ] Beta testing success metrics achieved
- [ ] Migration guide and tools ready
- [ ] Zero P0/P1 bugs outstanding

---

# Detailed Implementation Plan

Phase 1: Project Scaffolding and Foundation
Initialize Go Module: Create a new directory (e.g. hydra-go/) and initialize a Go module for the project. For example:
bash
Copy
mkdir hydra-go && cd hydra-go
go mod init github.com/Someblueman/hydra
This sets the module path to match the repository’s import path.
Choose a CLI Library: Use a robust CLI framework instead of manual flag parsing. Given Hydra’s many subcommands and flags
GitHub
 (e.g. spawn, kill, list, switch, etc.), Cobra is a great choice. It will simplify command definition and argument parsing, and can auto-generate shell completions (replacing the current completion script). Alternatively, urfave/cli could work, but Cobra’s support for subcommands fits Hydra’s structure well.
Scaffold Project Structure: Establish a clear project layout for the Go code. For example:
plaintext
Copy
hydra-go/
├── cmd/              # Main application entry point and Cobra commands
│   └── hydra/
│       └── main.go   # Initializes Cobra and defines commands
├── internal/         # Internal packages for core logic
│   ├── git/          # Git command wrappers (was lib/git.sh in shell)
│   │   └── git.go
│   ├── tmux/         # Tmux command wrappers (was lib/tmux.sh)
│   │   └── tmux.go
│   ├── state/        # State management (was lib/state.sh for branch-session map)
│   │   └── state.go
│   ├── github/       # GitHub issue integration logic (new, for --issue flag)
│   │   └── github.go
│   └── ai/           # (Optional) AI tool management (validate allowed commands, etc.)
│       └── ai.go
├── Makefile          # Build and test tasks
└── go.mod
Rationale: The internal packages mirror the original shell script modules for easy mapping of functionality. We’ve added a github package to handle the new GitHub issues feature and an ai package to encapsulate AI tool handling (this could also be part of the state or tmux logic, but separating concerns can help testing). The cmd/hydra/main.go will set up the Cobra root command and subcommands.
Stub Out Commands with Cobra: Define the Cobra commands corresponding to Hydra’s features. Cobra makes it straightforward to create subcommands like spawn, kill, list, etc., each with their flags. For instance, spawn will have flags --layout, --count/-n, --ai, --agents, and --issue
GitHub
. Ensure running hydra --help shows all commands and global flags. This scaffolding step ensures the basic CLI structure is in place even before the logic is implemented.
Basic Build/Run Verification: Write a minimal main.go that prints the version or help, and run go build to produce an executable. This checks that the module and Cobra are set up correctly. You might define a version constant (e.g. const Version = "1.1.0-dev" initially) and have a version subcommand to echo it, similar to the shell’s hydra version output
GitHub
.
Phase 2: Core Logic Implementation
Now, implement the actual functionality in Go, replacing each shell script component with Go code. We will closely follow the behaviors of the shell version, adding detail for the new features.
Tmux Integration (internal/tmux/tmux.go)
Recreate the helper functions from lib/tmux.sh in Go, using os/exec to call the tmux binary:
Session Existence: func SessionExists(name string) bool – use tmux has-session -t <name> and check the exit status
GitHub
. Return true if the session exists (exit 0), false if tmux reports it doesn’t exist (non-zero exit, which in shell was caught by tmux_session_exists returning 1).
Session Creation: func CreateSession(name, startDir string) error – run tmux new-session -d -s <name> -c <startDir>. This matches the shell’s create_session logic (detached session in the given directory)
GitHub
GitHub
. Before running, check that the directory exists and session name isn’t empty. If tmux returns an error (e.g., session already exists
GitHub
), propagate that as a Go error.
Session Termination: func KillSession(name string) error – run tmux kill-session -t <name>. Only attempt if SessionExists(name) is true to avoid spurious errors
GitHub
. This corresponds to shell kill_session
GitHub
.
Send Keys to Session: func SendKeys(session, cmd string) error – wrapper for sending a command string to a tmux session’s active pane. Essentially: tmux send-keys -t <session> "<cmd>" Enter. In the shell, send_keys_to_session ensured the session exists and then injected the keys
GitHub
GitHub
. We must be careful to properly quote or escape the command string if needed. This function is critical for starting the AI tool inside the tmux session.
Switching Sessions: func SwitchSession(name string) error – if we’re inside tmux (check TMUX env var), execute tmux switch-client -t <name>; if outside tmux, execute tmux attach-session -t <name>
GitHub
. This mirrors switch_to_session in shell.
Other Utilities: Implement any other needed tmux calls, for example:
CurrentSession() string if needed (shell’s get_current_session returns the name of the current tmux session, or empty if not in tmux
GitHub
).
ListSessions() ([]string, error) if needed for the list command (shell uses tmux list-sessions)
GitHub
.
CheckTmuxVersion() error – run tmux -V and ensure it’s ≥ 3.0, since Hydra requires tmux 3.0+
GitHub
. If version is too low or tmux not found, return an error (the shell printed an error in check_tmux_version and returned 1).
We will use these tmux wrapper functions inside the CLI command implementations. Each should be unit-testable by mocking the exec calls (or by calling a real tmux in a controlled environment).
Git Integration (internal/git/git.go)
Port the logic from lib/git.sh to manage git branches and worktrees:
Branch Validation: Before any git operation, ensure branch names are safe. Implement func ValidateBranchName(name string) error reproducing rules from shell
GitHub
:
Disallow empty string.
Disallow names starting with - (to prevent them being interpreted as options).
Disallow dangerous characters (;|&\$(){}[]<>*'"etc.) and sequences like..` that could lead to path traversal
GitHub
.
Possibly impose a length limit (the shell uses 255 chars max)
GitHub
.
Return an error with a message if invalid, otherwise nil if ok.
Branch Existence: func BranchExists(name string) bool – use git rev-parse --verify on refs/heads/<name> and refs/remotes/origin/<name> (as the shell does)
GitHub
. If either command succeeds (exit 0), the branch exists. This helps decide whether to create a new branch or use an existing one when spawning.
Create Worktree: func CreateWorktree(branch, path string) error – this encapsulates the shell’s create_worktree function:
Validate the branch name (using the above function) to avoid injection or invalid names
GitHub
.
Validate/prepare the path: ensure the parent directory exists, similar to shell which does mkdir -p for the parent
GitHub
.
Determine if the branch already exists (BranchExists). If yes, call git worktree add <path> <branch>; if no, call git worktree add -b <branch> <path> to create a new branch in one step
GitHub
. Both commands should be run with -- to separate path in case of funny names (the shell script uses -- as well).
If the git command fails (non-zero exit), return error. Otherwise, on success, the new worktree is created at path.
Delete Worktree: func DeleteWorktree(path string, force bool) error – correspond to delete_worktree in shell:
Validate the path for safety (shell’s validate_worktree_path) – disallow empty path, and guard against .. sequences or important system directories
GitHub
.
Verify the directory actually exists; if not, return an error that the worktree is not found.
If force is true, run git worktree remove --force <path> to remove it unconditionally
GitHub
. If not forced:
Check for uncommitted changes: run git -C <path> diff and git -C <path> diff --cached. If either shows changes (non-zero exit means there are changes, since --quiet returns non-zero for changes), then we have uncommitted work
GitHub
.
Check for untracked files: git -C <path> ls-files --others --exclude-standard. If this outputs anything, there are untracked files
GitHub
.
If either of these conditions exist, the shell prints a warning and prompts the user to confirm removal
GitHub
. In Go, we should similarly warn and require confirmation (unless in a non-interactive/forced context). We can reuse a global --force flag or an env var to skip prompts in scripting scenarios (more on this below).
If the user declines, abort deletion with an appropriate message.
If safe to proceed, run git worktree remove <path> (without --force). This removes the worktree’s reference; we may also manually os.RemoveAll(path) afterward if the directory still exists, to ensure a full cleanup (the shell relies on git worktree remove, which usually deletes the dir).
Return an error if removal fails.
Other Git Helpers:
CurrentBranch() (string, error): run git branch --show-current to get the active branch name; used for status command to display current branch
GitHub
.
Possibly GitVersion() string: git --version for reporting.
Security Note: Also carry over the shell’s safety checks for running git commands. For example, Hydra’s shell scripts often use -- in git commands to mark the end of options (preventing a branch name like --help from messing with the command). Our Go implementation should do the same when invoking exec.Command for git, by passing parameters properly (e.g., exec.Command("git", "worktree", "add", "--", path, branch) to ensure path isn’t interpreted as an option).
By implementing these as functions in internal/git, the spawn, kill, and other commands can call them to handle repository operations reliably.
State Management (internal/state/state.go)
Hydra keeps track of active “heads” (parallel sessions) by maintaining a mapping between branch names and tmux session names. In the shell, this is in a file ~/.hydra/map, and functions like add_mapping, remove_mapping, etc., manipulate it
GitHub
GitHub
. We will reimplement this in Go:
In-Memory Map: Use a package-level variable or a struct that holds map[string]string mapping branch -> session. This will be populated on startup by reading the state file, and kept in sync with it when commands run.
Load/Save State File: Decide on a format:
For simplicity and backward-compatibility, you can keep the plaintext format: one line per mapping, e.g. "branchName sessionName". This is easy to parse (split on first space) and matches the current Hydra map file.
Alternatively, use JSON (e.g. a list of objects or a dictionary) to make parsing and writing atomic. JSON would be more structured and potentially extensible (if we ever store more info per head).
The plan suggested JSON for easier parsing, but plaintext is also trivial to handle in Go. We could also consider using Go’s encoding/csv or just string operations.
In either case, implement:
func LoadMappings(filePath string) (map[string]string, error) – reads the file (if it exists) and returns the mapping. If file is missing or empty, return an empty map (no error).
func SaveMappings(filePath string, mappings map[string]string) error – writes out the map. For safety, write to a temp file and then rename over the original (the shell does this to avoid clobbering the file on failure
GitHub
GitHub
). This prevents state corruption if the program crashes mid-write.
These will be called at program start (to initialize state) and whenever we modify the map (after a spawn or kill).
Add Mapping: func AddMapping(branch, session string) error – add an entry to the in-memory map and persist it. If that branch already had a mapping, overwrite it (the shell’s add_mapping removes any existing entry for the branch before adding
GitHub
). Return an error only if file writing fails.
Remove Mapping: func RemoveMapping(branch string) error – remove the entry from the map (if present) and update the file. In shell, this is done by filtering out the branch from the file (reading all lines except those matching, then rewriting)
GitHub
. In Go, just delete from the map and call Save. (We should handle the case of no mapping gracefully – not an error.)
Get Session/Branch: Provide helpers like GetSessionForBranch(branch string) (string, bool) and maybe GetBranchForSession(session string) (string, bool) if needed. The shell has both directions (get_session_for_branch and get_branch_for_session)
GitHub
GitHub
, though the primary use is branch -> session. These just do lookups in the map (the shell versions loop through the file; in Go it’s a simple map lookup).
List Mappings: func ListMappings() map[string]string or a formatted string – primarily for the list command. We can just range over the map and print entries in a sorted order (for consistent output), or in whatever order (the shell prints in insertion order as they are in file).
Validate Mappings (optional): The shell included validate_mappings and cleanup_mappings to detect stale entries (if a branch or session no longer exists)
GitHub
GitHub
. We can incorporate similar logic:
After loading, or periodically, check each mapping: if the git branch no longer exists or the tmux session is gone, warn or remove those entries.
This could be part of a hydra doctor or hydra status check rather than every startup. But making LoadMappings silently drop invalid ones (with a log message) could keep state clean. The shell’s cleanup_mappings is called in kill_all_sessions to remove orphaned entries as it goes
GitHub
GitHub
.
Generate Session Name: Hydra appends numbers if a session name conflicts. We’ll implement func GenerateSessionName(branch string) string to replace shell’s generate_session_name
GitHub
GitHub
:
Start with a base name (maybe just the branch name, with any disallowed tmux chars sanitized to _ as the shell does
GitHub
).
If no existing tmux session has that name (tmux.SessionExists(base)), use it. Otherwise, append _1, _2, etc. until a free name is found (with some upper limit to avoid infinite loops)
GitHub
.
Since our design uses branch name itself (or branch-1, branch-2 for multi-spawn, see below) as unique identifiers, collisions might be rare except when reusing branch names. But this function ensures even if two heads somehow want the same session name, we adjust one.
Overall, the state management in Go centralizes what was in the shell’s global file. By reading it into memory, operations like lookup and updates become easier and less error-prone (and we avoid repeatedly grepping the file). We just must remember to save after each change.
GitHub Issue Integration (internal/github/github.go)
One of the newly added features is the ability to spawn a session directly from a GitHub issue (hydra spawn --issue <number>). In the shell, this is handled via the GitHub CLI (gh) in lib/github.sh
GitHub
GitHub
 and integrated into cmd_spawn logic
GitHub
GitHub
. To implement this in Go:
Dependency Check: Provide a function like func EnsureGhCLI() error that checks if the gh CLI is installed (exec.LookPath("gh")) and perhaps if the user is logged in (gh auth status). The shell’s check_gh_cli does both, returning an error message if not available
GitHub
GitHub
. In Go, if either check fails, return an error explaining the need to install or authenticate GitHub CLI. The spawn --issue command should call this before proceeding.
Fetch Issue Data: func FetchIssueDetails(issueNum int) (title string, state string, err error) – use gh to get issue info. For example:
go
Copy
cmd := exec.Command("gh", "issue", "view", fmt.Sprintf("%d", issueNum), "--json", "number,title,state")
Capture cmd.Stdout and run it. Parse the JSON output using Go’s encoding/json into a struct or map. (The shell uses sed to extract fields
GitHub
, but we can parse properly.) We need the issue title and state:
Title: used to construct the branch name.
State: to check if the issue is closed. If state == "CLOSED", the shell prints a warning and asks user confirmation
GitHub
. We should do the same (prompt "Issue is closed, continue? [y/N]").
If the gh command fails (non-zero exit), return an error (the shell would print “Failed to fetch issue #X”
GitHub
). Also handle the case of being in a non-git repository; actually gh issue view likely requires a GitHub repo context, but presumably the user runs hydra in a cloned repo directory.
Generate Branch Name from Issue: func IssueBranchName(issueNum int, issueTitle string) string – produce a branch name like issue-<number>-<sanitized-title>. Follow the same rules as shell’s generate_branch_from_issue:
Lowercase the title, replace spaces with hyphens, remove special chars, collapse multiple hyphens, trim hyphens from ends
GitHub
.
Prefix with issue-<num>-
GitHub
.
Ensure it’s not too long (the shell cuts to 50 chars of title
GitHub
, plus the prefix).
The shell then ensures it doesn’t end in a hyphen
GitHub
, but our sanitization already covers trimming.
We can use regex or strings.Map in Go for this sanitization. The result is the suggested new branch name.
Integration into Spawn: In the Cobra spawn command logic, detect the --issue flag. If --issue is provided:
Ensure no regular branch name was also given (mutually exclusive usage)
GitHub
.
Ensure incompatible flags aren’t used (-n or --agents cannot be combined with --issue in the current design
GitHub
).
Call EnsureGhCLI(), then FetchIssueDetails(issueNum). If error or missing data, print error and exit.
Get the title and state; if state is closed, prompt the user to confirm proceeding (unless perhaps a --force flag is provided to skip this prompt – the shell always prompts, but we could allow an override if desired).
Compute the branch name via IssueBranchName.
Then, proceed as if hydra spawn <branchName> was called: i.e., call our spawn logic with that branch name. The user doesn’t manually supply a branch in this mode, but our code will now create a worktree for issue-123-some-feature (for example) and a tmux session accordingly.
Git Integration Consideration: We should clarify that spawning from an issue assumes the local repo doesn’t already have a branch of that name. If it does, git worktree add -b will fail since branch exists. The shell’s approach is: if branch exists, create_worktree will just add it (since git_branch_exists returns true and they don’t use -b in that case)
GitHub
. So if the issue branch already exists locally, Hydra will just create a worktree from it (potentially an existing branch with maybe different title – edge case but possible). We should handle that seamlessly with our CreateWorktree logic (which already checks branch existence).
In summary, this GitHub integration in Go largely shells out to gh as in the shell version, with Go providing structured parsing and error handling. We will also document that this feature requires the GitHub CLI to be installed and authenticated
GitHub
.
AI Tools and Multi-Agent Workflows (internal/ai/ai.go or part of spawn logic)
Hydra’s hallmark is launching AI coding assistants inside tmux sessions. Recent updates added bulk spawning and mixed agents support for multi-AI workflows
GitHub
GitHub
. We need to replicate these:
Supported AI Commands Allowlist: Define the list of allowed AI tool commands: e.g. []string{"claude", "codex", "cursor", "copilot", "aider", "gemini"}. The shell’s validate_ai_command uses a case statement to allow these exact names
GitHub
. Implement func ValidateAICommand(name string) error that checks if name is in the allowed list. If not, return an error like “Unsupported AI command: X. Supported: claude, codex, ...”
GitHub
. This prevents arbitrary shell commands from being injected via the --ai flag.
Default AI Tool: Hydra uses an env var HYDRA_AI_COMMAND for the default tool (defaulting to "claude" if not set)
GitHub
. In our Go program, we can read os.Getenv("HYDRA_AI_COMMAND") in the spawn logic. If the user didn’t provide --ai, we use the env or default. Validate that choice with ValidateAICommand too, just in case the env is set to something invalid.
Spawning a Single Session with AI: For the normal spawn <branch> (count = 1, no agents spec):
Use the git wrapper to create the worktree.
Use tmux wrapper to create a new session for that branch.
After the tmux session is created, we launch the AI tool inside it:
Determine the AI command: use --ai flag if provided, else default as above.
Call ValidateAICommand on it
GitHub
. If invalid, print error and abort the spawn (the shell aborts if validation fails).
Use tmux.SendKeys(session, aiCmd) to send the AI command into the session, followed by an Enter. The shell does this with send_keys_to_session "$session" "$ai_tool"
GitHub
, which effectively starts the assistant.
Print a message to the user like “Starting <AI tool> in session …” to mirror shell output
GitHub
.
Add the branch->session mapping to state (this actually should happen before launching the AI, right after session creation, just as the shell does
GitHub
GitHub
, so that even if AI launch fails, the session is known).
Bulk Spawn (-n option): If the user requests hydra spawn <branch> -n N for N > 1, we need to create multiple parallel sessions:
The shell handles this in spawn_bulk
GitHub
. In Go, we can implement it either within the spawn command handler or as a helper function SpawnMultiple(baseBranch string, count int, layout, ai string) error.
Steps:
Confirm with user if count is large (the shell prompts if >3)
GitHub
. We can do a similar prompt using fmt.Printf and reading input from os.Stdin. If not confirmed, abort.
Loop i from 1 to count:
Construct branchName = baseBranch + "-" + i (e.g. "feature-x-1")
GitHub
.
Print progress (“Creating head 'branchName'...”)
GitHub
.
Call the single-session spawn logic for that branch (with the chosen layout and ai). This will create the worktree, tmux, etc.
Track successes and failures. The shell increments succeeded or failed counts and collects created branch names in a list
GitHub
.
If a spawn fails (spawn_single returns error), the shell asks if the user wants to continue with the remaining or abort and rollback
GitHub
:
If user chooses to abort, it calls cmd_kill on each successfully created branch so far to clean them up. We should do the equivalent: for each branch in our success list, call our Kill logic (with force/non-interactive to avoid prompting during rollback).
Then return an error to stop the bulk spawn.
If user chooses to continue after a failure, just loop to the next without rolling back.
After the loop, print a summary: e.g. “Bulk spawn complete: Succeeded: X, Failed: Y”.
Attach to the first session automatically if any succeeded and if running interactively. The shell checks if stdout is a TTY and then switches to the first session created
GitHub
. In Go, we can detect interactivity (os.Stdin is terminal) using a library or syscall.IsTerminal; if true, use tmux.SwitchSession(firstSession) to attach. This is a nice usability touch so the user is dropped into the first new head immediately.
Mixed Agents (--agents option): This is more complex bulk spawning where the user specifies multiple AI tools and how many of each, e.g. --agents "claude:2,aider:1" for 3 sessions (2 using Claude, 1 using Aider)
GitHub
GitHub
.
In the shell, spawn_bulk_mixed parses the spec, validates each agent and count, and then performs a loop similar to bulk spawn
GitHub
GitHub
.
In Go:
Parse the agents spec string. Split by commas to get segments, then by : to separate agent name and count. Trim spaces. If any segment is malformed or count not a positive integer, return an error (shell prints “Invalid spec” and exits in that case
GitHub
).
Validate each agent name with ValidateAICommand (shell does this for each agent in the spec loop
GitHub
).
Sum up total count of sessions across all agents.
Prompt for confirmation if total > 3 (same reasoning as before)
GitHub
.
Loop through each agent specification:
For each agent, loop for its count:
Construct branch name as baseBranch + "-" + sessionNum (where sessionNum increments globally across all agents)
GitHub
GitHub
. Example: base "exp", spec "claude:2,aider:1" yields branch names exp-1 (Claude), exp-2 (Claude), exp-3 (Aider).
Print progress “[i/total] Creating head 'branchName' with <agent>...”
GitHub
.
Call single-session spawn with that branch and agent as the AI tool.
Track success/failure, and handle failure similarly with user prompt to continue or rollback all created so far
GitHub
GitHub
.
Continue until all agents processed or aborted on failure.
Print summary (succeeded vs failed)
GitHub
 and potentially auto-switch to the first session (same as bulk spawn)
GitHub
.
This mixed mode reuses a lot of logic from bulk spawn, just varying the AI tool per iteration. We should ensure the state mapping uses distinct branch names for each session (which our naming scheme does), and that CreateWorktree is called with each of those branch names (creating new Git branches if they don’t exist). The shell approach creates all as new branches if base branch didn’t exist; in Go, CreateWorktree will handle new vs existing branch automatically. It might be wise to require that the base branch (without suffix) exists or not; the shell’s design treats base just as a prefix for naming, not an actual branch to branch off. In practice, git worktree add -b exp-1 will create a new branch exp-1 from HEAD for the first session, etc. Users likely expect to branch off the current HEAD for each new head.
Implementation within Cobra: The spawn command logic should decide which code path to take:
If --issue flag is set, we handle the GitHub integration (as above) and then do a single spawn.
Else, if --agents flag is set (non-empty string), call the mixed-agent spawning function.
Else, if --count/-n > 1, call the uniform bulk spawn function.
Else, just a single spawn.
(The shell enforces that --agents and -n can’t be used together or with --issue
GitHub
 – we should do the same validation and give a clear error if misused.)
Gemini Support: The new “gemini” tool is just one of the allowed AI commands, so by adding it to the allowlist we cover it. We should also note in documentation (Phase 5) any special setup needed (the README mentions Node.js 18+ requirement for Gemini
GitHub
). The code itself doesn’t need to treat it specially beyond validation. When SendKeys(session, "gemini") runs, it will launch the Gemini CLI (if installed). As an enhancement, we might detect if gemini is not installed and warn (similar to how we check for gh CLI), but that might be beyond scope – the user will see “command not found” in the tmux session if not installed.
By implementing the above, we ensure Hydra’s AI integration and multi-session capabilities in Go match the shell version’s new features. These are complex flows with lots of edge cases (failures, user confirmations), so careful testing will be needed (as planned in Phase 3).
Implementing the Hydra Commands (Cobra Command Handlers)
With the core packages in place, build out each command’s logic using those packages:
spawn Command: This is the most involved command, tying together git, tmux, state, AI, and GitHub integration:
Parse flags (--layout, --count, --ai, --agents, --issue). Cobra will handle flag parsing; our code just uses the values.
If --issue is set, perform the GitHub flow to get a branch name (as detailed above), then call single spawn.
Otherwise, if --agents is set or --count > 1, handle bulk spawning accordingly.
For each head created (single or in loops):
Validate we are inside a git repository (the shell does git rev-parse at start of spawn_single
GitHub
 – we should call git rev-parse or our CurrentBranch() and error out if not a repo).
Possibly ensure required tools are present: check tmux via CheckTmuxVersion() (shell does this at top of spawn_single
GitHub
), and for AI maybe ensure the specified AI command is in PATH (shell doesn’t explicitly check the command exists, but that could be a nice addition – e.g., if user says --ai codex but has no codex command, tmux will just error inside the session).
Use internal/git.CreateWorktree(branch, path) to create the new worktree for the head. The path can be <repo_parent>/hydra-<branch> (same convention as shell)
GitHub
GitHub
. We get the repo root via git rev-parse --show-toplevel.
Use internal/tmux.CreateSession(sessionName, path) to start tmux. For sessionName, generate one from branch (especially important if branch name has special chars or already taken) using GenerateSessionName.
If tmux session creation fails, rollback the worktree (shell deletes the worktree if tmux fails to start
GitHub
GitHub
). So call DeleteWorktree(path, force=false) to remove the new worktree directory if the tmux session couldn’t be made.
If session created, add the mapping via state.AddMapping(branch, session). The shell warns but doesn’t abort if mapping fails to save
GitHub
 (low chance of error unless disk issue).
If a layout was specified (other than "default"), apply it: in shell they do tmux send-keys -t session "TMUX=$TMUX . /usr/local/lib/hydra/layout.sh && apply_layout <layout>" Enter
GitHub
. In Go, since our layouts will be implemented either internally or we might just replicate this approach:
We could re-implement the layout logic in Go (like splitting panes accordingly – see Dashboard & Layout below), or simply call tmux with equivalent commands. However, since layout is often a quick combination of tmux commands, we might incorporate a simplified approach: for now, perhaps just support the same three layouts (“default”, “dev”, “full”) and call a small helper that runs the appropriate tmux split-window and tmux select-layout sequence. This can be done via exec.Command("tmux", ...) calls.
Alternatively, as a shortcut, we could invoke a shell snippet as the current version does (loading the shell layout script). But that ties us to the old script; better to do it natively in Go by invoking tmux commands directly.
Then handle launching the AI tool as described (validate and SendKeys the command).
On success, if this was a single spawn (not part of bulk), optionally attach to the new session immediately (the shell doesn’t auto-attach for single spawn; it just prints the session name and leaves the user to hydra switch or manually attach. In Hydra usage, typically you run hydra spawn outside tmux and it auto-attaches you to the new session because the process ends and tmux takes over. Actually, need to confirm: The shell’s spawn, after doing everything, calls switch_to_session? It doesn’t appear to, except in bulk mode it does for convenience
GitHub
. For single spawn, Hydra likely prints nothing or just returns. Probably we keep that behavior: don’t auto-attach on single spawn because the user might want to script spawns. We can consider adding an option in the future.)
kill Command:
Flags: --all, --force. Parse those.
If --all is true, ignore any branch argument (and error if both branch and --all given)
GitHub
.
If --all: call an internal KillAllSessions(force bool) error that implements the logic described earlier:
Load the current mappings (from state).
If none, print “No active Hydra heads to kill”
GitHub
 and exit gracefully (not an error).
If some, list them to the user for confirmation
GitHub
.
If not forced, prompt the user Kill all X heads? [y/N]
GitHub
. If decline, abort.
If in a non-interactive environment and not forced, refuse to proceed (shell prints an error in that scenario to prevent accidental mass deletion in scripts)
GitHub
.
Then iterate through each mapping:
If the tmux session exists, attempt to kill it (tmux.KillSession). If that succeeds, also remove the mapping (state) and delete the worktree directory
GitHub
GitHub
. Track success count.
If the tmux session doesn’t exist (maybe a stale mapping), still remove mapping and try to delete the worktree if present (shell prints a notice and does that cleanup
GitHub
GitHub
).
If any session fails to kill (e.g. tmux command fails), count a failure and continue to next
GitHub
GitHub
.
After loop, print summary “Succeeded: N, Failed: M”
GitHub
. If any failures, return an error (shell returns 1 if any failure occurred during kill all
GitHub
).
If a specific branch name is given (and not --all):
Look up the session name from state (GetSessionForBranch). If none, print “No session found for branch X” (not an error, or maybe return an error code 1 – shell prints a message and returns 1 in that case
GitHub
).
If found, prompt confirmation (unless --force or non-interactive mode). The shell checks if running interactively and not forced, then asks “Kill hydra head 'branch'? [y/N]”
GitHub
. If no, abort the kill with “Aborted”.
Then perform tmux.KillSession(session) (if session exists)
GitHub
.
Remove the mapping via state and delete the worktree directory (similar to above, using our DeleteWorktree)
GitHub
GitHub
.
Print a message that the head is killed.
Use the --force flag to skip confirmation prompts in both single and all. Also, as mentioned, consider honoring an env var like HYDRA_NONINTERACTIVE to automatically behave as --force in test or script environments (the shell uses this to bypass confirmations in tests)
GitHub
.
One more thing: after killing, if the user is currently inside the tmux session they killed, they might get detached or see it close. In shell usage, if you run hydra kill from inside that session, the script would probably kill the session while running – which might terminate the script prematurely. The shell might avoid that by running outside or by design. In our Go version, if user tries that, tmux kill-session will terminate the session (and our process) immediately. This is tricky to avoid; we could detect TMUX env and if killing current session, maybe detach the process first or warn user. This is an edge case, but a note for reliability (not necessarily in plan detail, but something to consider).
list Command:
Fetch the mappings from state. If none or map file empty, print “No active Hydra heads” (shell does this)
GitHub
.
Otherwise, for each mapping, check if the tmux session is live (tmux.SessionExists). Then print either e.g. ✓ branch -> session or some indicator for dead sessions. The shell cmd_status prints a similar list with checkmarks for active and "dead" for missing sessions
GitHub
. The list command in shell likely just lists names; but we can improve by indicating if any session is dead (or maybe status covers that). The README suggests hydra list shows active heads; possibly it omits dead ones.
We can simply list each branch and session, or just branch names. Decide based on current behavior: The README example for list is just “List all active Hydra heads”
GitHub
, which likely prints branch names (maybe along with session names). We might print “branch (session)” or similar.
No external calls needed except possibly to validate session existence.
switch Command:
This should allow the user to select an active head and attach to it. The shell’s interactive switching uses fzf if available
GitHub
:
If fzf is in PATH, Hydra shell pipes the list of sessions to fzf for selection.
If not, it enumerates the sessions and prompts the user to enter a number.
In Go, we have a few options:
The simplest: detect fzf in environment (exec.LookPath("fzf")). If found, run fzf by feeding it the list of branch names (or branch -> session lines) via stdin and capture the selected output. Then find that branch and call tmux.SwitchSession(session).
If fzf not found, implement a fallback: print each active head with an index and read user input from stdin (like “Enter the number of the head to switch to:”). This is straightforward to code.
This interactive behavior might require our process to be attached to a terminal. Cobra might consume input, but we can use fmt.Scanln or bufio.NewReader(os.Stdin) for reading.
Once we have the target (either branch or session name), call tmux.SwitchSession(targetSession). The tmux wrapper will handle whether we’re inside/outside tmux.
If the user aborts (ESC or empty selection), just exit without switching.
We should also handle if the selected session is somehow not found (perhaps a race where it was killed while selecting) – just error out gracefully.
regenerate Command:
Recreate tmux sessions for existing worktrees in case Hydra was restarted or tmux crashed. The shell implementation:
Ensures it’s run inside a git repo (so we know where to look for worktrees)
GitHub
.
Looks in the parent directory of the repo for any directories named hydra-*
GitHub
.
For each such directory: extract the branch name (strip the hydra- prefix)
GitHub
.
Skip if a session for that branch already exists (so we don’t duplicate)
GitHub
.
Otherwise, generate a session name and create a new tmux session attached to that worktree directory
GitHub
.
Add the mapping and count it as regenerated.
Print summary of how many sessions created vs skipped (already existed)
GitHub
.
For Go, we can do exactly that:
Find the repo root via git rev-parse (or our own function).
Get the parent dir, list contents, filter for hydra- prefix.
For each, see if it corresponds to an entry in our state map:
If not in state or state has it but tmux session is not running (i.e., a “dead” session
GitHub
), then we want to regenerate it.
We might even use our state file to know which heads existed – but state may be empty if Hydra was completely restarted. Looking at the filesystem catches cases where the state file was lost but worktree dirs remain.
For each to regen: use internal/tmux.CreateSession(newSessionName, path) and then state.AddMapping(branch, newSessionName).
Print results.
This command helps users recover from unexpected exits and should be preserved as-is.
status Command:
Summarize the system and Hydra state. In shell, cmd_status prints system info (Hydra version, tmux version, git version) and repository info (path, current branch)
GitHub
GitHub
, then lists active vs dead sessions and gives counts
GitHub
GitHub
.
In Go:
Print Hydra version (from our constant) and maybe build info if available.
Print tmux -V output and git --version output, or say “not installed” if these commands fail
GitHub
.
If in a git repo, show the repo path and current branch
GitHub
.
Load the state (or use in-memory map) to count active vs dead sessions:
For each mapping, check tmux.SessionExists(session). Increment an “active” counter if true, “dead” counter if false
GitHub
.
We can list them as well, similar to list but marking dead ones.
Finally, print counts and maybe a hint like the shell does (“Note: Dead sessions can be regenerated...”)
GitHub
.
This is mostly informational, so we can format nicely. No special new code needed beyond using previous components.
doctor Command:
A deeper health-check and performance test suite. The shell’s cmd_doctor does:
Checks for dependencies (tmux, git) and their versions, marks if they meet requirements or not
GitHub
GitHub
.
Measures command dispatch latency by timing a call to hydra version
GitHub
 (nanosecond precision if available).
Checks the state file existence, size, and number of entries
GitHub
.
Summarizes if any issues were found (if missing tools, etc.)
GitHub
.
In Go, we can implement similar:
Check if tmux and git executables are present and correct version (for tmux use CheckTmuxVersion and for git maybe ensure ≥2.x if we have a requirement, or simply note version).
Use Go’s time functions to measure how quickly we can execute a subcommand or perform certain operations. Given the Go version will likely be faster than shell, this may always be minimal, but it’s still a good diagnostic.
Check the state file (~/.hydra/map): report its location, size, and number of lines (entries). We can use our loaded map length for count.
Print any warnings or errors (e.g., outdated tmux).
Overall, ensure this command doesn’t modify anything, just reports.
This is a lower priority to get perfect, but since Hydra has it, we’ll implement it to match user expectations. It’s useful for troubleshooting.
dashboard Command:
The dashboard is an advanced feature that aggregates panes from all sessions into one tmux session for overview. The shell’s show_dashboard (triggered by hydra dashboard) works as follows:
Creates a new tmux session named "hydra-dashboard"
GitHub
.
Iterates over all active sessions (from the map) and for each:
Gets the first pane of the first window in that session
GitHub
.
Records its pane ID, session and window ID in a restore file (to put them back later)
GitHub
.
Renames that pane’s title to the branch name
GitHub
 for clarity.
Moves (joins) the pane into the dashboard session’s window
GitHub
. The first pane becomes the base, others join it.
Arranges the layout of the dashboard session’s window depending on number of panes (1, 2, 3, 4, etc.)
GitHub
GitHub
, using tmux select-layout presets.
Binds the q key in the dashboard session to trigger an exit (which calls a dashboard-exit command that restores all panes to original sessions)
GitHub
.
When user presses q or runs hydra dashboard-exit, it iterates over the recorded pane info and uses tmux join-pane to move each pane back to its original session/window
GitHub
.
Reimplementing this in Go is tricky but doable with direct tmux calls:
You can still orchestrate it by calling tmux commands via exec in sequence. For example, to collect panes:
Use tmux list-panes -t <session> -F "#{pane_id} #{window_id}" to get the pane and window, as the shell does
GitHub
.
Use tmux select-pane -T <title> to set the pane title (shell uses -T flag)
GitHub
.
Use tmux join-pane -s <pane_id> -t hydra-dashboard:0 to move it.
Keep track of original locations in a list in memory (instead of a file as shell uses DASHBOARD_RESTORE_MAP).
For layout, use tmux select-layout similarly by counting panes.
For exiting, since our Go program will likely exit after setting up the dashboard (because we hand control to tmux dashboard session), we might implement dashboard-exit as a subcommand that the tmux key binding calls. The shell binds hydra dashboard-exit to the q key
GitHub
.
We can have Cobra define a hidden command dashboard-exit that when invoked, connects to the running dashboard session and restores panes:
It would read the saved pane info (perhaps we need to save it in a file like shell does, or store in a tmp file).
Then perform the restore by calling tmux join-pane -s <pane_id> -t <session:window> for each recorded pane
GitHub
.
And finally kill the dashboard session.
This is a rather complex interaction because it requires the Go program to be invoked from within tmux context. An alternative is to embed the restore logic into the dashboard session via tmux commands (maybe using tmux’s after or hooks), but using the dashboard-exit subcommand as in shell is fine.
Given the complexity, one approach during migration is to temporarily mark dashboard as unsupported or implement a simplified version (like just a message “Dashboard is not yet available in Go version” for an initial release). However, to fully replace Hydra’s features, implementing it will be expected by users. We aim to incorporate it if time permits, or clearly document if it’s deferred.
Cycle-layout: Another minor command that just triggers a layout change in the current tmux session. In shell, cycle_layout is executed inside tmux (bound to Ctrl-L or via command) to toggle between “default -> dev -> full -> default...” layouts
GitHub
GitHub
. In Go, hydra cycle-layout can call tmux select-layout commands:
Determine current layout (perhaps by counting panes or using a heuristic like shell’s get_current_layout did
GitHub
).
Choose next layout and apply (which is essentially calling our layout functions or tmux commands).
This command is typically triggered from within a tmux session, so it should operate on TMUX env’s current session. We might just call the tmux select-layout cycles as in shell.
Completion: Cobra can auto-generate completion scripts for bash/zsh/fish, so implement hydra completion [shell] by calling Cobra’s Root().GenBashCompletion(os.Stdout) etc., or use the built-in command that some Cobra setups provide. This replaces generate_completion in shell.
In implementing each command, we’ll continuously refer to Hydra’s main shell script to ensure we replicate all options and messages. The usage text in the new Hydra should remain the same as the current one (for familiarity), including examples of multi-agent usage and GitHub issues
GitHub
.
Phase 3: Testing 🧪
With all functionalities in place, we must rigorously test the new Go-based Hydra. Both unit tests and integration tests are important:
Unit Tests (Function-Level): Write tests for each critical function in internal/ packages:
tmux package: We can create a fake tmux binary or use environment variables to simulate tmux’s presence. However, it might be easier to run tests expecting a real tmux:
For example, test SessionExists by actually creating a dummy session (using exec.Command("tmux", "new-session", "-d", "-s", "testsession") in the test setup) and then calling our SessionExists("testsession") to assert it returns true, and SessionExists("nosuch") returns false.
Test CreateSession by creating one and then ensure SessionExists is true, etc. Cleanup by killing it after.
Test SendKeys by sending a command that writes to a file or environment and then reading it. Or simpler: we could attach to the session and verify output – but that’s complex for unit test. Instead, trust that if no error is returned, tmux accepted the keys.
These tests might be marked to skip if tmux is not available, or we ensure CI always has tmux.
Alternatively, abstract out the exec.Command calls behind an interface so we can inject a fake in tests (e.g., a fake executor that pretends a session exists). This adds complexity but can isolate logic.
git package: Use a temporary directory for a dummy git repo:
In test setup, do git init, create a commit, etc., so that we can test BranchExists, CreateWorktree, DeleteWorktree.
For CreateWorktree, create a new branch via the function and assert that a new directory appears and git branch shows the branch.
For DeleteWorktree, test non-forced scenario: create a dummy file in the worktree, mark it modified, and ensure DeleteWorktree returns an error about uncommitted changes. Then test forced deletion removes the directory.
Test branch name validation with various inputs (empty, with spaces, with .., etc.) to ensure it catches invalid ones.
state package: Since we don’t want to manipulate the real ~/.hydra/map during tests, have the state functions take a path (so we can use a temp file for testing).
Test adding mappings: add a few, ensure the file has those lines and the map in memory reflects them.
Test removing: remove one and ensure it’s gone from map and file, others remain.
Test that adding a mapping for an existing branch replaces the old session (not duplicating lines).
Test GenerateSessionName: simulate that a session exists (maybe by creating one in tmux, or by faking SessionExists result if we abstract it) to see that it appends a suffix.
GitHub integration: This one is tricky to test fully without hitting the GitHub API. We can avoid external calls by mocking the gh command:
For unit tests, create an interface for executing commands (so we can inject a fake that returns preset JSON). For example, have FetchIssueDetails call an internal function runGhIssueView(number) that we override in tests.
Feed it sample JSON strings to simulate open and closed issues and ensure parsing works (e.g., given {"number":42,"title":"Bug fix","state":"OPEN"}, it returns title "Bug fix").
Test that closed issues trigger the confirmation prompt logic. We might simulate user input by temporarily redirecting stdin in the test (or better, refactor the code to allow injecting a “yes” answer for test).
AI and spawn logic:
Test ValidateAICommand with valid and invalid inputs (should accept "claude", "gemini"
GitHub
, reject "maliciouscmd").
The spawn functions themselves (bulk, mixed) are harder to unit-test in isolation because they involve creating sessions and processes. Many of those can be covered by integration tests instead (see below). For unit tests, we might just test the parsing logic of the --agents string (ensuring it splits and validates correctly).
If we structure spawn logic in smaller pieces (e.g., separate function to parse flags into a config struct, separate to perform the actions given a config), we could test those pieces. But full end-to-end spawn likely in integration tests.
Integration Tests (CLI-Level): We want to mirror the comprehensive shell test suite (over 100 tests) in spirit
GitHub
, verifying the Go hydra behaves the same for end-user scenarios.
A straightforward approach is to write bash scripts similar to the existing ones (e.g. tests/test_github.sh, test_tmux.sh) but targeting the new binary. However, since we are writing in Go, we can also use Go’s testing framework to spawn subprocesses:
Use exec.Command("./hydra", args...) in tests to run the compiled CLI with various inputs, then check the results (files created, output text, etc.).
For example, integration test for spawn:
In a temp directory, git init a repo and create an initial commit.
Run hydra spawn testbranch in that directory.
Verify: a new directory ../hydra-testbranch is created (worktree), a new tmux session named (something like testbranch or derived) exists, and ~/.hydra/map has an entry for testbranch.
Perhaps run hydra list and capture output to see that "testbranch" is listed.
Then run hydra kill testbranch --force and verify the session is gone (tmux session killed, worktree removed, map entry removed).
Integration test for spawn --issue:
We can simulate this by running a local GitHub CLI in a dummy repo. A simpler approach: since we can’t easily create a fake GitHub API on the fly, we might skip calling the real gh in test and instead factor our code to allow substituting the issue fetch (as mentioned). Or run it in a way that we know will fail and check it prints the proper error message “Failed to fetch issue”.
Possibly mark GitHub integration tests to be skipped unless certain conditions (like an env var with a GitHub token) are present.
Integration test for spawn -n and --agents:
Run hydra spawn feature -n 2 --ai aider and check that it created feature-1 and feature-2 heads, both with aider running. We can attach to one or check ps processes to confirm the aider process is running in tmux. Or simply rely on hydra list showing them.
Test hydra spawn exp --agents "claude:1,aider:1". Verify two heads created (exp-1, exp-2) and mappings exist.
Also intentionally cause a failure during bulk spawn to test rollback: e.g., perhaps use an invalid AI tool for the second head to force a failure and see if the first head is cleaned up. (This might require contriving an invalid scenario, since normally if the first succeeded and second fails, user would be prompted. We can simulate answering "n" to continue, which triggers rollback. Automating that in a test means we need to feed "n" to the prompt. We can do this by running the command in a pseudo-tty or by echoing "n" into its stdin.)
Integration test for kill --all:
Create multiple heads, then run hydra kill --all --force.
Verify all tmux sessions killed and worktree dirs gone, and state file cleared.
Also test --all without --force in an interactive context (this is tricky to automate, might skip or simulate input).
Integration test for switch:
Spawn two sessions, then run hydra switch in a non-interactive way by piping an answer. For example, simulate no fzf installed: ensure our code falls back to numeric. We can pipe "2\n" to hydra switch via stdin to choose the second session.
Verify that after switch, the current tmux session is the chosen one (if we run the test inside tmux).
Alternatively, for testing, we might not be inside tmux, so hydra switch would attach to the session and effectively the process won't return until that tmux session is detached. This is hard to test in an automated way. We might skip deep testing of switch, or test its selection logic in isolation (like feed it input and see that it picks the right session to switch to, without actually switching in a headless test).
The dashboard and cycle-layout features would also need integration tests, but those might be even harder to automate. The shell’s test suite likely has something for them (e.g., verifying that after dashboard exit all sessions are restored). We could emulate some of that if possible.
CI Configuration: We will ensure that the CI (Phase 4) runs these tests in an environment with tmux, git, and (optionally) fzf and gh installed. The original CI installed tmux and even ran tests with dash shell
GitHub
. For Go, we’ll install tmux and git on the runner as needed, and not worry about dash. If possible, include fzf and gh in the CI for full coverage of those features (or at least test that our program gracefully handles their absence).
By writing thorough tests, we’ll catch discrepancies between the old and new implementation. The aim is that all key scenarios covered by the shell tests are also covered for the Go version, giving confidence in parity. (We should also consider a period of manual testing for interactive behavior and perhaps beta releases for community to try the Go version.)
Phase 4: Build, Distribution, and CI/CD
With code and tests ready, adapt the build and deployment process for the new Go project:
Makefile Updates: Simplify and update the Makefile for Go:
Build: a build target that runs GO111MODULE=on go build -o bin/hydra ./cmd/hydra. We might output the binary to a bin/ directory to mirror how the shell had a bin/hydra script, or directly to the project root. Ensure the binary name is simply hydra for installation.
Test: a test target that runs go test -v ./.... We might include -race flag for race detection in tests, and possibly separate unit vs integration tests (with tags or by package).
Lint/Format: add a lint target if we want to run golangci-lint or go vet, and a fmt target for gofmt -s -w . to keep code style consistent. (These replace shellcheck/dash validations.)
Remove any targets related to shell validation (no more dash -n or shellcheck). The original CI had a dedicated job for POSIX compliance
GitHub
, which we no longer need.
Possibly keep the clean target to remove build artifacts, and install/uninstall if they exist (adjusted for binary, see below).
Install/Uninstall Scripts: Modify install.sh and uninstall.sh for the Go binary:
install.sh: Instead of copying multiple files to /usr/local, we only need to copy the compiled hydra binary (and maybe the man page or completion scripts if we decide to ship those). So:
Ensure hydra is built (the script might call make build or instruct the user to build first).
Copy hydra to /usr/local/bin/hydra (prompt for sudo if needed). The shell version also copied library scripts to /usr/local/lib/hydra – that’s no longer needed since logic is in the binary.
Possibly copy the completion scripts to an appropriate location if we pre-generate them, or instruct the user how to generate them via the command itself.
Create ~/.hydra/ directory if not present (the shell’s init_hydra_home does this on the fly
GitHub
, but we can also ensure the installer creates the config directory).
uninstall.sh: Remove the /usr/local/bin/hydra binary. Also remove /usr/local/lib/hydra if it was used (e.g., from a previous shell installation) to avoid confusion – but caution the user if they had any custom modifications there. Remove any other installed support files (completions, etc., if applicable).
Update any messages in these scripts to reflect the new installation steps (no mention of POSIX or shell).
Continuous Integration (GitHub Actions):
We will overhaul the CI workflow:
Setup Go: Use actions/setup-go to install the desired Go version (e.g., Go 1.20+) on the runner.
Install Dependencies: Ensure git and tmux (and fzf, gh if possible) are installed in the runner environment for tests. On Ubuntu runner, this is via apt-get as before
GitHub
. We no longer need dash installed for testing, but having it doesn’t hurt (not used though).
Build & Test: Add steps to run make build and make test. Since we removed the separate lint job for shell, we might introduce a lint step for Go (like run golangci-lint if configured, or at least go vet ./...).
Parallel jobs: The previous CI had jobs for compatibility across shells/OS
GitHub
. We can drop those. Instead, we could introduce a matrix for OS (Linux, macOS, maybe Windows) for building and testing:
On macOS, we can install brew packages for tmux to run tests. On Windows, tmux and the concept of forked processes for CLI might not work at all; Hydra might not be intended for Windows use (tmux doesn’t run on Windows natively). We could skip Windows in CI, or if we aim to support WSL only.
At least ensure Linux and macOS compatibility since Hydra likely had macOS users (the shell was POSIX and should have worked on mac).
Artifact builds: Optionally, add a job to build release binaries for various platforms. This can be done with go build for each OS/ARCH combination. If not doing that now, we at least ensure the code is portable (no Linux-specific assumptions).
Remove shell-specific CI steps entirely (shell lint, dash compatibility, etc.)
GitHub
GitHub
.
CI Test Behavior: Running integration tests in CI might need some tweaks:
We might mark some tests as “slow” or “requires tmux” and allow them to be skipped if tmux isn’t present. But since we plan to install tmux, just ensure tests handle scenarios gracefully if, say, tmux refuses to start (maybe in a headless environment tmux needs a TERM set; our CI should provide a TERM).
Might need to run tmux kill-server at the start/end of tests to ensure a clean slate (multiple test runs could interfere if sessions persist).
Ensure the CI user has permission to create /usr/local/bin etc., if our tests or install script attempt that. We might not run install script in CI (just build and test).
Versioning and Release Prep:
Since this Go migration is a major change, consider bumping Hydra’s version to 2.0.0 (semantic versioning: major rewrite). The shell’s latest was 1.1.0
GitHub
; going to 2.0.0 signals big changes. Update the version constant and CHANGELOG accordingly.
Plan for a transitional release: for example, tag 2.0.0-beta for testing, then 2.0.0 final.
After merging the Go code to main, ensure that creating a GitHub Release triggers building the binaries. This can be automated with a GitHub Action or done manually. If using GoReleaser is overkill for now, a simple approach: manually run GOOS=linux go build ..., GOOS=darwin go build ... for distribution. But eventually, an automated pipeline for releases is beneficial.
Backwards Compatibility:
Note that after this migration, users will have a single binary. We should ensure that if they update via install.sh, the old shell scripts (if any in /usr/local/bin/hydra or /usr/local/lib/hydra) are overwritten or removed to avoid confusion. Our install script should maybe remove /usr/local/lib/hydra directory entirely, because it’s obsolete – but only do so if it’s exactly the old structure (to avoid deleting anything unintended).
Inform users that any aliases or direct sourcing of shell libraries (unlikely, but just in case) won’t apply anymore.
This phase ensures the Go version is properly built and delivered, and that CI gates the quality (tests passing). By removing shell-specific checks and adding Go build/test, the CI will focus on the new code’s health.
Phase 5: Documentation 📚
Finally, update documentation to reflect the new implementation and any user-facing changes:
README.md:
Update the description: Hydra is no longer a “POSIX-compliant shell tool”
GitHub
, but a Go-based CLI tool. Emphasize the benefits of the Go rewrite (speed, maintainability) if desired, but also make clear it functions the same way for users.
Features: Ensure the feature list still applies. For example, all features listed (tmux integration, git worktrees, GitHub integration, session persistence, interactive switching, performance monitoring) remain true
GitHub
. We might add that it’s cross-platform (if we officially support macOS/Windows now).
Requirements: Remove the requirement for /bin/sh
GitHub
. Instead, list Go (only for building from source, not needed for using a released binary), plus the runtime requirements:
git, tmux (same as before)
GitHub
,
the AI tool CLIs (Claude, etc.) – unchanged
GitHub
,
Node.js for Gemini (unchanged)
GitHub
,
fzf for interactive switch (still optional)
GitHub
,
GitHub CLI for issues (still optional)
GitHub
.
These remain relevant, so just tweak wording to say “optional, for these features” accordingly.
Installation: If previously it suggested make install or running install.sh, update if needed (maybe now just go install github.com/Someblueman/hydra could be an option for Go-savvy users, or downloading the binary from releases).
Usage Examples: They likely remain the same (the goal is that users see no difference in how they use Hydra). Double-check that nothing changed in CLI syntax:
The examples in README (spawn, list, switch, kill, dashboard, etc.)
GitHub
GitHub
 should still be valid. We should ensure our Cobra setup uses the same command names and flags (which we have aimed to do).
If we happen to add any new flags or alter behavior (ideally not without good reason), reflect that here. But currently, the plan is to keep interface identical.
Possibly add a note that Hydra is now implemented in Go internally, but that might not matter to end users except if we want to thank those who helped or mention performance improvements.
Ensure the new commands (like hydra completion) are documented if relevant, and that any mention of internal implementation (the old README mentioned “strictly POSIX-compliant and validated with ShellCheck and dash”
GitHub
) is removed or replaced with something like “implemented in Go”.
CONTRIBUTING.md:
This likely contained guidelines for shell scripting (formatting, quoting, etc.). We need to rewrite it for Go:
Explain coding style (e.g., follow Go best practices, use gofmt – possibly enforce via CI or pre-commit).
Mention how to add new dependencies (use Go Modules, run go mod tidy).
Testing: describe how to run the test suite (make test or go test). Encourage writing tests with any new code.
Remove any references to shell-specific things (no need for ShellCheck, no need to test on multiple shells, etc.).
If there were any notes about POSIX compliance, replace with notes about cross-platform considerations in Go (for instance, avoid using OS-specific syscalls so it compiles on Windows, etc., if we care about that).
Update any sections about project structure reflecting the new cmd/ and internal/ layout.
CHANGELOG.md:
Add a new entry for this migration release. For example:
Changed: “Entire codebase reimplemented in Go. All commands and options are the same, but the internals have changed from shell scripts to a Go binary
GitHub
. This improves performance and maintainability.”
Added: if during the rewrite we added anything new (perhaps we didn’t explicitly plan new features beyond those already in 1.1.0, but maybe we might include something like better error messages or cross-platform support – if so, list them).
Removed: any minor feature that we choose not to carry over (hopefully none; aim for feature parity. One possible omission could be extremely shell-specific behavior that doesn’t translate, but looking at features, all can be ported).
Reference that features introduced in 1.1.0 (issue integration, bulk spawn, etc.) are of course present. It might be worth explicitly stating those were carried over to reassure users: e.g., “Carried over all v1.1.0 features: GitHub issue integration
GitHub
, bulk spawn and multi-agent support
GitHub
, kill --all
GitHub
, Gemini support
GitHub
, etc.”
If version is bumped to 2.0.0, note the breaking changes if any (ideally none in CLI usage; maybe just that the installation method changed slightly or that the state file format might have changed if we chose JSON – if we stick to the same plain text, the state is backward compatible).
Keep previous entries for history. The changelog will inform users that this release is a big overhaul.
Man Pages / Help Output:
If Hydra had a man page or if --help text is used as documentation, ensure it’s updated. Cobra will generate usage from our code comments or definitions for each command, so make sure those descriptions are clear and complete (e.g., each flag has a help message).
Consider including examples in the help output (Cobra allows adding examples string). We can take the ones from README
GitHub
 and put them in the spawn command’s Examples field so hydra help spawn shows them.
Provide information about environment variables like HYDRA_HOME and HYDRA_AI_COMMAND in the help or README (the shell’s usage() lists those
GitHub
 – we should ensure our Cobra CLI mentions or the README does).
Migration Notice:
It could be helpful to add a short note in README or release notes that “Hydra is now written in Go as of this version – if you encounter any issues that differ from the previous shell version, please report them.” This sets expectation that behavior should be the same, but invites feedback on any discrepancies.
Testing/Development Docs:
If there’s a section in README or CONTRIBUTING about running tests or developing (the README had a “Development” section mentioning ShellCheck and dash usage)
GitHub
, update that:
e.g., “Run make test to execute the Go test suite. Ensure you have tmux installed for integration tests.”
Remove references to make lint with ShellCheck; maybe replace with make lint running golangci-lint if we set that up.
By updating the documentation thoroughly, users and contributors will smoothly transition to the new version. The key is to maintain clarity that, aside from the internal rewrite, Hydra should function as before (all examples and workflows remain valid). Any new quirks or slight differences should be noted to avoid confusion.
