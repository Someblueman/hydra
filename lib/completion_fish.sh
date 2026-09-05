#!/bin/sh
# Fish completion generator for Hydra.

# Generate fish completion script
# Usage: generate_fish_completion
# Returns: Fish completion script on stdout
generate_fish_completion() {
    cat <<'EOF'
# Fish completion for hydra

# Complete commands
complete -c hydra -f -n '__fish_use_subcommand' -a 'remote' -d 'Manage OpenSSH aliases'
complete -c hydra -f -n '__fish_use_subcommand' -a 'fleet' -d 'Inspect trusted remote installations'
complete -c hydra -f -n '__fish_seen_subcommand_from remote' -a 'add remove list'
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and not __fish_seen_subcommand_from task' -a 'handshake list doctor bootstrap package task init spawn signal cancel workflow attach export import reconcile watch tui'
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -a 'prepare inspect submit start status cancel logs help'
complete -c hydra -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l source -r
complete -c hydra -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l spec -r
complete -c hydra -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l input -r
complete -c hydra -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l output -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l key -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l id -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task' -l trust-spec -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet' -l json
complete -c hydra -f -n '__fish_seen_subcommand_from fleet' -l jobs -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet' -l timeout -r
complete -c hydra -f -n '__fish_use_subcommand' -a 'spawn' -d 'Create a new worktree and tmux session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'init' -d 'Initialize project identity, trust, profile, and worktree root'
complete -c hydra -f -n '__fish_use_subcommand' -a 'agent' -d 'Manage agent profiles'
complete -c hydra -f -n '__fish_use_subcommand' -a 'capabilities' -d 'Print machine-readable capabilities'
complete -c hydra -f -n '__fish_use_subcommand' -a 'workflow' -d 'Run or inspect a finite trusted workflow DAG'
complete -c hydra -f -n '__fish_use_subcommand' -a 'path' -d 'Print a stored worktree path'
complete -c hydra -f -n '__fish_use_subcommand' -a 'lifecycle' -d 'Show declared, observed, and live head state'
complete -c hydra -f -n '__fish_use_subcommand' -a 'outcome' -d 'Declare an instance-scoped outcome'
complete -c hydra -f -n '__fish_use_subcommand' -a 'wait' -d 'Wait for durable lifecycle evidence'
complete -c hydra -f -n '__fish_use_subcommand' -a 'adapter' -d 'Ingest provider-neutral lifecycle events'
complete -c hydra -f -n '__fish_use_subcommand' -a 'resume' -d 'Resume a head as a new instance'
complete -c hydra -f -n '__fish_use_subcommand' -a 'notify' -d 'Configure lifecycle notifications'
complete -c hydra -f -n '__fish_use_subcommand' -a 'exec' -d 'Run a bounded command across selected worktrees'
complete -c hydra -f -n '__fish_use_subcommand' -a 'diff' -d 'Show changes from the recorded base reference'
complete -c hydra -f -n '__fish_use_subcommand' -a 'review' -d 'Summarize Git review evidence'
complete -c hydra -f -n '__fish_use_subcommand' -a 'provenance' -d 'Show recorded head provenance'
complete -c hydra -f -n '__fish_use_subcommand' -a 'claim' -d 'Manage expiring path intent claims'
complete -c hydra -f -n '__fish_use_subcommand' -a 'scope' -d 'Set or check per-head read/write scopes'
complete -c hydra -f -n '__fish_use_subcommand' -a 'collision' -d 'Classify possible cross-head conflicts'
complete -c hydra -f -n '__fish_use_subcommand' -a 'resource' -d 'Manage per-head resources'
complete -c hydra -f -n '__fish_use_subcommand' -a 'gate' -d 'Capture and approve verification evidence'
complete -c hydra -f -n '__fish_use_subcommand' -a 'context' -d 'Create a typed context pack'
complete -c hydra -f -n '__fish_use_subcommand' -a 'sync' -d 'Safely merge a named ref into a head'
complete -c hydra -f -n '__fish_use_subcommand' -a 'land' -d 'Safely merge an approved head into the current branch'
complete -c hydra -f -n '__fish_use_subcommand' -a 'integrate' -d 'Verify and promote ordered candidates in an isolated worktree'
complete -c hydra -f -n '__fish_use_subcommand' -a 'du' -d 'Report per-head disk usage'
complete -c hydra -f -n '__fish_use_subcommand' -a 'gc' -d 'Apply a named cleanup policy'
complete -c hydra -f -n '__fish_use_subcommand' -a 'worktree' -d 'Run worktree doctor actions'
complete -c hydra -f -n '__fish_use_subcommand' -a 'snapshot' -d 'Emit canonical state JSON'
complete -c hydra -f -n '__fish_use_subcommand' -a 'list' -d 'List all active Hydra heads'
complete -c hydra -f -n '__fish_use_subcommand' -a 'switch' -d 'Switch to a different head'
complete -c hydra -f -n '__fish_use_subcommand' -a 'kill' -d 'Remove a worktree and its tmux session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'regenerate' -d 'Restore tmux sessions for existing worktrees'
complete -c hydra -f -n '__fish_use_subcommand' -a 'state' -d 'Verify, back up, migrate, or roll back durable state'
complete -c hydra -f -n '__fish_use_subcommand' -a 'events' -d 'Verify, tail, filter, retain, or repair head events'
complete -c hydra -f -n '__fish_use_subcommand' -a 'status' -d 'Show health status of all heads'
complete -c hydra -f -n '__fish_use_subcommand' -a 'doctor' -d 'Check system performance'
complete -c hydra -f -n '__fish_use_subcommand' -a 'dashboard' -d 'View all sessions in a single dashboard'
complete -c hydra -f -n '__fish_use_subcommand' -a 'dashboard-exit' -d 'Exit the dashboard session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'cycle-layout' -d 'Cycle through tmux pane layouts'
complete -c hydra -f -n '__fish_use_subcommand' -a 'tui' -d 'Interactive terminal UI for session management'
complete -c hydra -f -n '__fish_seen_subcommand_from tui' -l basic -d 'Use maintained shell TUI'
complete -c hydra -f -n '__fish_seen_subcommand_from tui' -l capabilities -d 'Show TUI capability diagnostics'
complete -c hydra -f -n '__fish_use_subcommand' -a 'cleanup' -d 'Remove orphaned worktrees and mappings'
complete -c hydra -f -n '__fish_use_subcommand' -a 'pr' -d 'Create or show GitHub PR for a session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'template' -d 'Manage session templates'
complete -c hydra -f -n '__fish_use_subcommand' -a 'completion' -d 'Generate shell completion scripts'
complete -c hydra -f -n '__fish_use_subcommand' -a 'version' -d 'Show version information'
complete -c hydra -f -n '__fish_use_subcommand' -a 'help' -d 'Show help message'
complete -c hydra -f -n '__fish_use_subcommand' -a 'group' -d 'Show or set session groups'
complete -c hydra -f -n '__fish_use_subcommand' -a 'send' -d 'Queue a message to a session inbox'
complete -c hydra -f -n '__fish_use_subcommand' -a 'recv' -d 'Read messages for the current session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'tail' -d 'View output from a session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'broadcast' -d 'Send a command to sessions'
complete -c hydra -f -n '__fish_use_subcommand' -a 'wait-idle' -d 'Wait for sessions to become idle'
complete -c hydra -f -n '__fish_use_subcommand' -a 'queue' -d 'Inspect or process the spawn queue'

# Complete flags
complete -c hydra -f -n '__fish_use_subcommand' -s h -l help -d 'Show help message'
complete -c hydra -f -n '__fish_use_subcommand' -s v -l version -d 'Show version information'

# Complete spawn command
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s l -l layout -d 'Layout to use' -a 'default dev full'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s n -l count -d 'Number of sessions to spawn' -a '1 2 3 4 5 6 7 8 9 10'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l ai -d 'AI tool to use' -a 'claude aider codex cursor copilot gemini'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l profile -d 'Agent profile' -a 'none claude codex cursor copilot aider gemini'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l no-agent -d 'Create a plain shell head'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l dry-run -d 'Print plan without mutation'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l prompt -d 'Task text'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l prompt-file -d 'Read task from file'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l issue-body -d 'Use issue body as task'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l completion-policy -d 'Completion evidence policy' -a 'declared-done observed-exit-zero either'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l scope-read -d 'Record a read-only path pattern'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l scope-write -d 'Record a writable path pattern'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l agents -d 'Mixed agents specification (e.g., claude:2,aider:1)'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s i -l issue -d 'Create from GitHub issue number'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l pr -d 'Create from GitHub PR number'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l pr-new -d 'Create new draft PR after spawn'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -l after -d 'Wait for dependencies (comma-separated branches)'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn' -s t -l template -d 'Apply session template'
complete -c hydra -f -n '__fish_seen_subcommand_from spawn; and not __fish_seen_subcommand_from -l --layout -n --count --ai --agents -i --issue --pr --pr-new --after -t --template' -a '(git branch 2>/dev/null | sed "s/^[ *]*//" | grep -v "^(")'

# Complete list command
complete -c hydra -f -n '__fish_seen_subcommand_from list' -l json -d 'Output in JSON format'
complete -c hydra -f -n '__fish_seen_subcommand_from list' -l deps -d 'Show dependency tree'
complete -c hydra -f -n '__fish_seen_subcommand_from list' -l git -d 'Show Git evidence from the recorded base'
complete -c hydra -f -n '__fish_seen_subcommand_from list' -l no-pr-status -d 'Skip fetching PR status'
complete -c hydra -f -n '__fish_seen_subcommand_from list' -l refresh-pr-status -d 'Force refresh PR status cache'

# Complete exec and Git evidence commands
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l branch -d 'Select a head'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l group -d 'Select a group'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l all -d 'Select all heads'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l jobs -d 'Maximum parallel commands' -a '1 2 4 8 16'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l timeout -d 'Per-command timeout in seconds'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l json -d 'Output versioned JSON'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l shell -d 'Execute an explicitly trusted shell string'
complete -c hydra -f -n '__fish_seen_subcommand_from exec' -l allow-shell -d 'Acknowledge shell-string execution'
complete -c hydra -f -n '__fish_seen_subcommand_from diff' -l stat -d 'Show diff statistics'
complete -c hydra -f -n '__fish_seen_subcommand_from diff' -l name-only -d 'Show changed paths'
complete -c hydra -f -n '__fish_seen_subcommand_from diff review provenance' -l json -d 'Output versioned JSON'

# Complete template command
complete -c hydra -f -n '__fish_seen_subcommand_from template' -a 'list create show edit delete'
complete -c hydra -f -n '__fish_seen_subcommand_from agent' -a 'list show doctor init'
complete -c hydra -f -n '__fish_seen_subcommand_from state' -a 'verify backup migrate rollback'
complete -c hydra -f -n '__fish_seen_subcommand_from events' -a 'verify tail filter retain repair'
complete -c hydra -f -n '__fish_seen_subcommand_from adapter' -a 'ingest'
complete -c hydra -f -n '__fish_seen_subcommand_from notify' -a 'enable disable list test'
complete -c hydra -f -n '__fish_seen_subcommand_from claim' -a 'add list remove'
complete -c hydra -f -n '__fish_seen_subcommand_from scope' -a 'set show check'
complete -c hydra -f -n '__fish_seen_subcommand_from resource' -a 'allocate status env release'
complete -c hydra -f -n '__fish_seen_subcommand_from gate' -a 'run approve status'
complete -c hydra -f -n '__fish_seen_subcommand_from context' -a 'create'
complete -c hydra -f -n '__fish_seen_subcommand_from workflow' -a 'list show validate dry-run run status cancel resume'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -a 'train status report cancel resume approve promote cleanup'
complete -c hydra -f -n '__fish_seen_subcommand_from worktree' -a 'doctor'
complete -c hydra -f -n '__fish_seen_subcommand_from snapshot' -l native -d 'Try the optional read-only native helper'
complete -c hydra -f -n '__fish_seen_subcommand_from claim' -l path -d 'Path pattern'
complete -c hydra -f -n '__fish_seen_subcommand_from claim' -l access -d 'Access mode' -a 'read write'
complete -c hydra -f -n '__fish_seen_subcommand_from claim' -l reason -d 'Claim reason'
complete -c hydra -f -n '__fish_seen_subcommand_from claim' -l expires-at -d 'Expiry epoch'
complete -c hydra -f -n '__fish_seen_subcommand_from scope' -l read -d 'Read-only path pattern'
complete -c hydra -f -n '__fish_seen_subcommand_from scope' -l write -d 'Writable path pattern'
complete -c hydra -f -n '__fish_seen_subcommand_from resource' -l port -d 'Port allocation NAME=START-END'
complete -c hydra -f -n '__fish_seen_subcommand_from resource' -l compose-project -d 'Compose project name'
complete -c hydra -f -n '__fish_seen_subcommand_from resource' -l database -d 'Database name'
complete -c hydra -f -n '__fish_seen_subcommand_from gate' -l name -d 'Gate name'
complete -c hydra -f -n '__fish_seen_subcommand_from gate' -l by -d 'Approval actor'
complete -c hydra -f -n '__fish_seen_subcommand_from gate' -l reason -d 'Approval reason'
complete -c hydra -f -n '__fish_seen_subcommand_from context' -l diff -d 'Include selected diff'
complete -c hydra -f -n '__fish_seen_subcommand_from context' -l file -d 'Add a manifest file'
complete -c hydra -f -n '__fish_seen_subcommand_from context' -l note -d 'Add a note'
complete -c hydra -f -n '__fish_seen_subcommand_from context' -l history -d 'Include bounded history'
complete -c hydra -f -n '__fish_seen_subcommand_from context' -l artifact -d 'Add an artifact reference'
complete -c hydra -f -n '__fish_seen_subcommand_from sync' -l from -d 'Source ref'
complete -c hydra -f -n '__fish_seen_subcommand_from sync land' -l gate -d 'Approved gate'
complete -c hydra -f -n '__fish_seen_subcommand_from sync land' -l dry-run -d 'Simulate without mutation'
complete -c hydra -f -n '__fish_seen_subcommand_from land' -l into -d 'Current target branch'
complete -c hydra -f -n '__fish_seen_subcommand_from land' -l keep-head -d 'Keep source head after landing'
complete -c hydra -f -n '__fish_seen_subcommand_from workflow' -l json -d 'Output versioned status JSON'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l base -d 'Explicit base ref'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l target -d 'Local target ref'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l into -d 'Local target ref'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l dry-run -d 'Preview without mutation'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l execute -d 'Create and verify an isolated integration'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l gate -d 'Verification command'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l by -d 'Approval actor'
complete -c hydra -f -n '__fish_seen_subcommand_from integrate' -l apply -d 'Apply cleanup'
complete -c hydra -f -n '__fish_seen_subcommand_from gc' -l policy -d 'Cleanup policy' -a 'orphaned stopped archives'
complete -c hydra -f -n '__fish_seen_subcommand_from gc' -l apply -d 'Apply selected policy'
complete -c hydra -f -n '__fish_seen_subcommand_from gc worktree' -l dry-run -d 'Report without mutation'
complete -c hydra -f -n '__fish_seen_subcommand_from gc' -l include-dirty -d 'Allow explicit dirty removal'
complete -c hydra -f -n '__fish_seen_subcommand_from gc' -l older-than -d 'Archive age in days'
complete -c hydra -f -n '__fish_seen_subcommand_from worktree' -l reason -d 'Lock reason'
complete -c hydra -f -n '__fish_seen_subcommand_from worktree' -l apply -d 'Apply repair or prune'
complete -c hydra -f -n '__fish_seen_subcommand_from claim scope collision resource gate context du snapshot' -l json -d 'Output versioned JSON'

# Complete dashboard flags
complete -c hydra -f -n '__fish_seen_subcommand_from dashboard' -s p -l panes-per-session -d 'Panes to collect per session' -a '1 2 3 4 5 6 7 8 9 10 all'

# Complete kill command with git branches
complete -c hydra -f -n '__fish_seen_subcommand_from kill' -a '(git branch 2>/dev/null | sed "s/^[ *]*//" | grep -v "^(")'
complete -c hydra -f -n '__fish_seen_subcommand_from kill' -l all -d 'Kill all hydra sessions'
complete -c hydra -f -n '__fish_seen_subcommand_from kill' -l force -d 'Skip confirmation prompt'
complete -c hydra -f -n '__fish_seen_subcommand_from kill' -l transcript -d 'Transcript policy' -a 'none redacted full'

# Complete switch command with active Hydra heads
complete -c hydra -f -n '__fish_seen_subcommand_from switch' -a '(hydra list --json 2>/dev/null | tr "{" "\n" | sed -n "s/.*\\\"branch\\\": \\\"\\([^\\\"]*\\)\\\".*/\\1/p")'

# Complete pr command with hydra sessions
complete -c hydra -f -n '__fish_seen_subcommand_from pr' -a '(hydra list --json 2>/dev/null | tr "{" "\n" | sed -n "s/.*\\\"branch\\\": \\\"\\([^\\\"]*\\)\\\".*/\\1/p")'
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task; and __fish_seen_subcommand_from logs' -l stream -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task; and __fish_seen_subcommand_from logs' -l offset -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task; and __fish_seen_subcommand_from logs' -l limit -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task; and __fish_seen_subcommand_from logs' -l step -r
complete -c hydra -f -n '__fish_seen_subcommand_from fleet; and __fish_seen_subcommand_from task; and __fish_seen_subcommand_from logs' -l attempt -r
EOF
}
