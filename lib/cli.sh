#!/bin/sh
# Hydra usage text and top-level command dispatcher.

# Display usage information
usage() {
    cat <<EOF
hydra - Manage parallel AI coding sessions with tmux and git worktree

Usage: hydra <command> [options]

Commands:
  spawn <branch>    Create a new worktree and tmux session
                    Options:
                      -l, --layout <layout>    Apply tmux layout
                      -n, --count <number>     Spawn multiple sessions (1-10)
                      --ai <tool>              Specify AI tool (claude, aider, gemini, etc.)
                      --profile <name>         Select a launch/resume profile
                      --no-agent              Create a first-class plain shell head
                      --dry-run               Print the resolved plan without mutation
                      --prompt <text>          Record and inject task text at launch
                      --prompt-file <path>     Read task text from a file
                      --issue-body             Use --issue body as task text
                      --completion-policy <p>  declared-done, observed-exit-zero, or either
                      --scope-read <pattern>   Inject and record a read-only path scope
                      --scope-write <pattern>  Inject and record a writable path scope
                      --agents <spec>          Mixed agents (e.g., "claude:2,aider:1")
                      -g, --group <name>       Assign to a group for bulk operations
                      --after <deps>           Wait for sessions to complete (e.g., "branch1,branch2")
                      --pr-new                 Create a draft PR after spawning
                      -t, --template <name>    Apply session template
  spawn --issue <#> Create a head from a GitHub issue
  spawn --pr <#>    Create a head from an existing GitHub PR
  init              Initialize project identity, trust, profile, and worktree root
  agent             List, show, diagnose, or initialize agent profiles
  capabilities      Print machine-readable Hydra and agent capabilities
  workflow          Run or inspect finite, recoverable workflow definitions
                    Usage: hydra workflow list
                           hydra workflow <show|validate|dry-run|run> <id|path>
                           hydra workflow <status|cancel|resume> <run-id>
  path [<branch>]   Print the stored worktree path for a head
  lifecycle <head>  Show declared outcome, observed status, and liveness
  outcome <head>    Declare an instance-scoped outcome
  wait <head>       Wait for durable lifecycle evidence
  adapter ingest    Ingest a canonical provider-neutral adapter event
  resume <head>     Create a new instance using durable resume metadata
  notify            Configure rate-limited local lifecycle notifications
  exec              Run an out-of-band command in selected worktrees
                    Options:
                      --branch <head>          Select one head (repeatable)
                      --group <name>           Select one group
                      --all                    Select all active project heads
                      --jobs <1-16>            Bounded parallelism (default 4)
                      --timeout <seconds>      Per-command timeout (default 300)
                      --json                   Versioned results
                      --shell <text>           Trusted shell-string mode
                      --allow-shell            Explicit shell execution acknowledgement
  diff <head>       Show changes from the recorded base reference
  review <head>     Summarize Git review evidence from the recorded base
  provenance <head> Show recorded task, profile, version, and trust provenance
  claim             Add, list, or remove expiring path intent claims
  scope             Set, show, or check read/write head scopes
  collision         Classify claim, overlap, predicted, and observed conflicts
  resource          Allocate, inspect, export, or release per-head resources
  gate              Run verification gates and record explicit approvals
  context           Create typed, explicit context packs without implicit files
  sync <head>       Safely merge a named ref into a clean, approved head
  land <head>       Safely merge an approved head into the current target branch
  integrate         Assemble, verify, approve, and promote completed heads
                    Use "integrate train" for per-candidate guarded gates
  du                Report per-head worktree and durable-state disk usage
  gc                Apply explicit orphaned, stopped, or archive cleanup policies
  worktree doctor   Inspect, lock, unlock, move, repair, or prune Git worktrees
  snapshot          Emit canonical state JSON; --native tries the optional core
  list              List all active Hydra heads
                    Options:
                      -g, --group <name>       Filter by group
                      --groups                 List all groups with session counts
                      -j, --json               Output in JSON format
                      --deps                   Show dependency tree
                      --git                    Show recorded-base Git evidence
                      --no-pr-status           Skip fetching PR status (faster)
                      --refresh-pr-status      Force refresh PR status cache
  switch [branch]   Switch to a head directly, or choose interactively
  kill <branch>     Remove a worktree and its tmux session
  kill --all        Kill all hydra sessions
  kill --group <n>  Kill all sessions in a group
                    Options:
                      --force          Skip confirmation prompt
  group <branch>    Show or set group for a session
                    Usage:
                      group <branch>           Show current group
                      group <branch> <name>    Set group
                      group <branch> --clear   Remove from group
                      group create <name> <branch> [branch...]
                                               Create group with sessions
                      group wait <name>        Wait for group to complete
                      group status <name>      Show group health status
                    Options for subcommands:
                      -t, --timeout <N>        Wait timeout in seconds
                      -j, --json               JSON output for status
  send <branch> <msg> Queue a message to another session's inbox
  recv              Read and clear messages for current session
                    Options:
                      --peek                   Don't remove messages
                      -j, --json               Output in JSON format
  pr [<branch>]     Create or show GitHub PR for a session
                    If no branch specified, uses current session
  tail <branch>     View output from a session
                    Options:
                      -n, --lines <N>          Number of lines (default: 50)
                      -f, --follow             Continuously watch output
  broadcast         Send a command to multiple sessions
                    Options:
                      -g, --group <name>       Target specific group
                      --pane <target>          Explicit tmux pane (window.pane or session:window.pane)
                      --force                  Send to :0.0 even if that pane is the agent
  wait-idle         Wait for sessions to become idle
                    Options:
                      -g, --group <name>       Wait for specific group
                      -s, --seconds <N>        Idle threshold (default: 10)
                      -t, --timeout <N>        Max wait time (default: 300)
  regenerate        Restore tmux sessions for existing worktrees
  state             Verify, back up, migrate, or roll back durable state
  events            Verify, tail, filter, retain, or repair head events
  status            Show health status of all heads
                    Options:
                      -j, --json               Output in JSON format
  doctor            Check install, dependencies, and first-run readiness
                    Options:
                      -f, --fix                Auto-fix detected issues
  cleanup           Remove dead mappings, stale locks, and orphaned worktrees
  dashboard         View all sessions in a single dashboard
                    Options:
                      -p, --panes-per-session <N|all>  Collect multiple panes per session
  tui               Native mission control with visible basic fallback
                    Options:
                      --basic                  Use the maintained shell TUI explicitly
                      --capabilities [--json]  Show TUI availability and dispatch policy
  cycle-layout      Cycle through tmux pane layouts
  queue             View and manage pending spawn queue
                    Subcommands:
                      queue                    List queued spawns
                      queue clear              Clear all queued spawns
                      queue remove <branch>    Remove specific entry
                      queue process            Process queue immediately
                    Options:
                      -j, --json               Output in JSON format
  completion        Generate shell completion scripts
  version           Show version information

Options:
  -h, --help        Show this help message

Examples:
  hydra spawn feature-x                    # Create single session
  hydra spawn feature-x -n 3               # Create 3 sessions (feature-x-1, feature-x-2, feature-x-3)
  hydra spawn feature-x -n 3 --ai aider    # Create 3 sessions with aider
  hydra spawn feature-x --ai gemini        # Create session with Google Gemini CLI
  hydra spawn exp --agents "claude:2,aider:1"  # Create 2 claude + 1 aider sessions
  hydra spawn --issue 42                   # Create session from GitHub issue #42
  hydra spawn feature-x -g project-a       # Create session in group 'project-a'
  hydra list --groups                      # Show all groups
  hydra list -g project-a                  # List sessions in group 'project-a'
  hydra group feature-x project-a          # Assign feature-x to group 'project-a'
  hydra tail feature-x                     # View last 50 lines of output
  hydra tail feature-x -f                  # Follow output in real-time
  hydra broadcast "git status"             # Send command to all sessions
  hydra broadcast -g project-a "make test" # Send command to group
  hydra wait-idle -g project-a             # Wait for group to finish
  hydra kill feature-x                     # Kill a specific session
  hydra kill --all                         # Kill all sessions (with confirmation)
  hydra kill --all --force                 # Kill all sessions without confirmation
  hydra kill -g project-a                  # Kill all sessions in group 'project-a'

Environment:
  HYDRA_HOME        Directory for runtime files (default: ~/.hydra)
  HYDRA_AI_COMMAND  Default AI tool (default: claude)
  HYDRA_SKIP_AI     Legacy shell-only switch (prefer spawn --no-agent)
  HYDRA_ROOT        Override hydra installation path (for library discovery)
  HYDRA_DASHBOARD_PANES_PER_SESSION  Panes per session for dashboard (1, N, or all)
  HYDRA_MAX_SESSIONS  Maximum active sessions (default: unlimited)
                      When limit is reached, spawns are queued for later
  HYDRA_SKIP_SETUP  Set to 1 to skip environment setup commands
  HYDRA_SETUP_CONTINUE  Set to 1 to continue spawn even if setup fails
  HYDRA_CORE        Explicit optional hydra-core executable
  HYDRA_CORE_TIMEOUT_SECONDS  Native handshake/command timeout (default: 2)
  HYDRA_TUI_BIN     Explicit optional hydra-tui executable
EOF
}

# Main command dispatcher
main() {
    HYDRA_JSON_REQUESTED=0
    case "${1:-}" in
        init|capabilities|lifecycle|wait|exec|diff|review|provenance|claim|scope|collision|resource|gate|context|du|snapshot|list|status|group|recv|queue)
            for _main_arg in "$@"; do
                case "$_main_arg" in
                    --) break ;;
                    -j|--json) HYDRA_JSON_REQUESTED=1 ;;
                esac
            done
            ;;
    esac
    export HYDRA_JSON_REQUESTED
    init_hydra_home

    # Load only the libraries needed for this command
    _load_libs_for_cmd "${1:-}"

    case "${1:-}" in
        spawn)
            shift
            cmd_spawn "$@"
            ;;
        init)
            shift
            cmd_init "$@"
            ;;
        agent)
            shift
            cmd_agent "$@"
            ;;
        capabilities)
            shift
            cmd_capabilities "$@"
            ;;
        path)
            shift
            cmd_path "$@"
            ;;
        workflow)
            shift
            cmd_workflow "$@"
            ;;
        lifecycle)
            shift
            cmd_lifecycle "$@"
            ;;
        outcome)
            shift
            cmd_outcome "$@"
            ;;
        wait)
            shift
            cmd_wait "$@"
            ;;
        adapter)
            shift
            cmd_adapter "$@"
            ;;
        resume)
            shift
            cmd_resume "$@"
            ;;
        notify)
            shift
            cmd_notify "$@"
            ;;
        exec)
            shift
            cmd_exec "$@"
            ;;
        diff)
            shift
            cmd_diff "$@"
            ;;
        review)
            shift
            cmd_review "$@"
            ;;
        provenance)
            shift
            cmd_provenance "$@"
            ;;
        claim)
            shift
            cmd_claim "$@"
            ;;
        scope)
            shift
            cmd_scope "$@"
            ;;
        collision)
            shift
            cmd_collision "$@"
            ;;
        resource)
            shift
            cmd_resource "$@"
            ;;
        gate)
            shift
            cmd_gate "$@"
            ;;
        context)
            shift
            cmd_context "$@"
            ;;
        sync)
            shift
            cmd_sync "$@"
            ;;
        land)
            shift
            cmd_land "$@"
            ;;
        integrate)
            shift
            cmd_integrate "$@"
            ;;
        du)
            shift
            cmd_du "$@"
            ;;
        gc)
            shift
            cmd_gc "$@"
            ;;
        worktree)
            shift
            cmd_worktree "$@"
            ;;
        snapshot)
            shift
            cmd_snapshot "$@"
            ;;
        list)
            shift
            cmd_list "$@"
            ;;
        switch)
            shift
            cmd_switch "$@"
            ;;
        kill)
            shift
            cmd_kill "$@"
            ;;
        group)
            shift
            cmd_group "$@"
            ;;
        send)
            shift
            cmd_send "$@"
            ;;
        recv)
            shift
            cmd_recv "$@"
            ;;
        pr)
            shift
            cmd_pr "$@"
            ;;
        template)
            shift
            cmd_template "$@"
            ;;
        tail)
            shift
            cmd_tail "$@"
            ;;
        broadcast)
            shift
            cmd_broadcast "$@"
            ;;
        wait-idle)
            shift
            cmd_wait_idle "$@"
            ;;
        regenerate)
            shift
            cmd_regenerate "$@"
            ;;
        state)
            shift
            cmd_state "$@"
            ;;
        events)
            shift
            cmd_events "$@"
            ;;
        status)
            shift
            cmd_status "$@"
            ;;
        doctor)
            shift
            cmd_doctor "$@"
            ;;
        cleanup)
            shift
            cmd_cleanup "$@"
            ;;
        dashboard)
            shift
            cmd_dashboard "$@"
            ;;
        dashboard-exit)
            shift
            cmd_dashboard_exit "$@"
            ;;
        tui)
            shift
            cmd_tui "$@"
            ;;
        cycle-layout)
            shift
            cmd_cycle_layout "$@"
            ;;
        completion)
            shift
            cmd_completion "$@"
            ;;
        queue)
            shift
            cmd_queue "$@"
            ;;
        version|-v|--version)
            echo "Hydra version $HYDRA_VERSION"
            ;;
        help|-h|--help)
            usage
            ;;
        *)
            if [ -z "${1:-}" ]; then
                usage
            else
                echo "Error: Unknown command '$1'" >&2
                echo "Run 'hydra help' for usage information" >&2
                exit 1
            fi
            ;;
    esac
}

