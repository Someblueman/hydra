#!/bin/sh
# Zsh completion generator for Hydra.

# Generate zsh completion script
# Usage: generate_zsh_completion
# Returns: Zsh completion script on stdout
generate_zsh_completion() {
    cat <<'EOF'
#compdef hydra
# Zsh completion for hydra

_hydra() {
    local context state line
    typeset -A opt_args
    
    _arguments -C \
        '1: :_hydra_commands' \
        '*::arg:->args' \
        && return 0
    
    case $state in
        args)
            case $words[1] in
                remote)
                    _arguments '1:action:(add remove list)' '*:argument:'
                    ;;
                fleet)
                    _arguments '1:action:(handshake list doctor bootstrap package init spawn signal cancel workflow attach export import reconcile watch tui)' '*:argument:'
                    ;;
                spawn)
                    _arguments \
                        '(-l --layout)'{-l,--layout}'[Layout to use]:layout:(default dev full)' \
                        '(-n --count)'{-n,--count}'[Number of sessions to spawn]:count:(1 2 3 4 5 6 7 8 9 10)' \
                        '--ai[AI tool to use]:ai:(claude aider codex cursor copilot gemini)' \
                        '--profile[Agent profile to use]:profile:(none claude codex cursor copilot aider gemini)' \
                        '--no-agent[Create a plain shell head]' \
                        '--dry-run[Print plan without mutation]' \
                        '--prompt[Task text]:task:' \
                        '--prompt-file[Read task from file]:file:_files' \
                        '--issue-body[Use issue body as task]' \
                        '*--scope-read[Read-only repository-relative scope]:pattern:' \
                        '*--scope-write[Writable repository-relative scope]:pattern:' \
                        '--agents[Mixed agents specification]:agents:' \
                        '(-i --issue)'{-i,--issue}'[Create from GitHub issue]:issue:' \
                        '--pr[Create from GitHub PR]:pr:' \
                        '--pr-new[Create new draft PR after spawn]' \
                        '--after[Wait for dependencies]:deps:_hydra_sessions' \
                        '(-t --template)'{-t,--template}'[Apply session template]:template:_hydra_templates' \
                        '1:branch:_hydra_branches'
                    ;;
                list)
                    _arguments \
                        '--json[Output in JSON format]' \
                        '--deps[Show dependency tree]' \
                        '--git[Show Git evidence from the recorded base]' \
                        '--no-pr-status[Skip fetching PR status]' \
                        '--refresh-pr-status[Force refresh PR status cache]'
                    ;;
                exec)
                    _arguments \
                        '*--branch[Select a head]:branch:_hydra_sessions' \
                        '--group[Select a group]:group:' \
                        '--all[Select all heads]' \
                        '--jobs[Maximum parallel commands]:jobs:(1 2 4 8 16)' \
                        '--timeout[Per-command timeout in seconds]:seconds:' \
                        '--json[Output versioned JSON]' \
                        '--shell[Execute an explicitly trusted shell string]:command:' \
                        '--allow-shell[Acknowledge shell-string execution]'
                    ;;
                diff)
                    _arguments '--stat[Show diff statistics]' '--name-only[Show changed paths]' '--json[Output versioned JSON]' '1:branch:_hydra_sessions'
                    ;;
                review|provenance)
                    _arguments '--json[Output versioned JSON]' '1:branch:_hydra_sessions'
                    ;;
                claim)
                    _arguments '1:subcommand:(add list remove)' '--path[Path pattern]:pattern:' '--access[Access mode]:access:(read write)' '--reason[Claim reason]:text:' '--expires-at[Expiry epoch]:epoch:' '--json[Output versioned JSON]'
                    ;;
                scope)
                    _arguments '1:subcommand:(set show check)' '*--read[Read-only path pattern]:pattern:' '*--write[Writable path pattern]:pattern:' '--json[Output versioned JSON]'
                    ;;
                collision)
                    _arguments '--json[Output versioned JSON]' '1:left head:_hydra_sessions' '2:right head:_hydra_sessions'
                    ;;
                resource)
                    _arguments '1:subcommand:(allocate status env release)' '*--port[Port allocation NAME=START-END]:range:' '--compose-project[Compose project name]:name:' '--database[Database name]:name:' '--json[Output versioned JSON]'
                    ;;
                gate)
                    _arguments '1:subcommand:(run approve status)' '--name[Gate name]:name:' '--by[Approval actor]:actor:' '--reason[Approval reason]:text:' '--json[Output versioned JSON]'
                    ;;
                context)
                    _arguments '1:subcommand:(create)' '--diff[Include selected diff]' '*--file[Add a manifest file]:file:_files' '--note[Add a note]:text:' '--history[Include bounded history]:count:' '*--artifact[Add an artifact reference]:artifact:_files' '--json[Output versioned JSON]'
                    ;;
                workflow)
                    _arguments '1:subcommand:(list show validate dry-run run status cancel resume)' '--json[Output versioned status JSON]' '2:workflow or run:'
                    ;;
                sync)
                    _arguments '--from[Source ref]:ref:' '--gate[Approved gate]:name:' '--dry-run[Simulate without mutation]' '1:head:_hydra_sessions'
                    ;;
                land)
                    _arguments '--into[Current target branch]:branch:_hydra_branches' '--gate[Approved gate]:name:' '--dry-run[Simulate without mutation]' '--keep-head[Keep source head after landing]' '1:head:_hydra_sessions'
                    ;;
                integrate)
                    _arguments '1:selector or subcommand:(train status report cancel resume approve promote cleanup)' '--base[Explicit base ref]:ref:' '--target[Local target ref]:ref:' '--into[Local target ref]:ref:' '--dry-run[Preview without mutation]' '--execute[Create and verify an isolated integration]' '*--gate[Verification command]:command:' '--by[Approval actor]:actor:' '--apply[Apply cleanup]'
                    ;;
                du)
                    _arguments '--json[Output versioned JSON]'
                    ;;
                gc)
                    _arguments '--policy[Cleanup policy]:policy:(orphaned stopped archives)' '--apply[Apply selected policy]' '--dry-run[Report without mutation]' '--include-dirty[Allow explicit dirty removal]' '--older-than[Archive age in days]:days:'
                    ;;
                worktree)
                    _arguments '1:subcommand:(doctor)' '2:action:(status lock unlock move repair prune)' '--reason[Lock reason]:text:' '--dry-run[Report without mutation]' '--apply[Apply repair or prune]'
                    ;;
                snapshot)
                    _arguments '--native[Try the optional read-only native helper]' '--json[Output canonical JSON]'
                    ;;
                tui)
                    _arguments '--basic[Use the maintained shell TUI]' '--capabilities[Show TUI capability diagnostics]'
                    ;;
                template)
                    _arguments '1:subcommand:(list create show edit delete)'
                    ;;
                state)
                    _arguments '1:subcommand:(verify backup migrate rollback)'
                    ;;
                events)
                    _arguments '1:subcommand:(verify tail filter retain repair)'
                    ;;
                dashboard)
                    _arguments \
                        '(-p --panes-per-session)'{-p,--panes-per-session}'[Panes to collect per session]:value:(1 2 3 4 5 6 7 8 9 10 all)'
                    ;;
                kill)
                    _arguments \
                        '--all[Kill all hydra sessions]' \
                        '--force[Skip confirmation prompt]' \
                        '1:branch:_hydra_branches'
                    ;;
                switch)
                    _arguments '1:session:_hydra_sessions'
                    ;;
                pr)
                    _arguments '1:branch:_hydra_sessions'
                    ;;
            esac
            ;;
    esac
}

_hydra_commands() {
    local commands; commands=(
        'remote:Manage OpenSSH aliases'
        'fleet:Inspect trusted remote installations'
        'spawn:Create a new worktree and tmux session'
        'init:Initialize project identity, trust, profile, and worktree root'
        'agent:Manage agent profiles'
        'capabilities:Print machine-readable capabilities'
        'workflow:Run or inspect a finite trusted workflow DAG'
        'path:Print a stored worktree path'
        'lifecycle:Show declared, observed, and live head state'
        'outcome:Declare an instance-scoped outcome'
        'wait:Wait for durable lifecycle evidence'
        'adapter:Ingest provider-neutral lifecycle events'
        'resume:Resume a head as a new instance'
        'notify:Configure lifecycle notifications'
        'exec:Run a bounded command across selected worktrees'
        'diff:Show changes from the recorded base reference'
        'review:Summarize Git review evidence'
        'provenance:Show recorded head provenance'
        'claim:Manage expiring path intent claims'
        'scope:Set or check per-head read/write scopes'
        'collision:Classify possible cross-head conflicts'
        'resource:Manage per-head ports and environment names'
        'gate:Capture and explicitly approve verification evidence'
        'context:Create a typed context pack'
        'sync:Safely merge a named ref into a head'
        'land:Safely merge an approved head into the current branch'
        'integrate:Verify and promote ordered candidates in an isolated worktree'
        'du:Report worktree and state disk usage'
        'gc:Apply a named cleanup policy'
        'worktree:Run worktree doctor actions'
        'snapshot:Emit canonical state JSON'
        'list:List all active Hydra heads'
        'switch:Switch to a different head'
        'kill:Remove a worktree and its tmux session'
        'regenerate:Restore tmux sessions for existing worktrees'
        'state:Verify, back up, migrate, or roll back durable state'
        'events:Verify, tail, filter, retain, or repair head events'
        'status:Show health status of all heads'
        'doctor:Check system performance'
        'dashboard:View all sessions in a single dashboard'
        'dashboard-exit:Exit the dashboard session'
        'cycle-layout:Cycle through tmux pane layouts'
        'tui:Interactive terminal UI for session management'
        'cleanup:Remove orphaned worktrees and mappings'
        'pr:Create or show GitHub PR for a session'
        'template:Manage session templates'
        'completion:Generate shell completion scripts'
        'version:Show version information'
        'help:Show help message'
        'group:Show or set session groups'
        'send:Queue a message to a session inbox'
        'recv:Read messages for the current session'
        'tail:View output from a session'
        'broadcast:Send a command to sessions'
        'wait-idle:Wait for sessions to become idle'
        'queue:Inspect or process the spawn queue'
    )
    _describe 'command' commands
}

_hydra_branches() {
    local branches
    branches=(${(f)"$(git branch 2>/dev/null | sed 's/^[ *]*//' | grep -v '^(')"})
    _describe 'branch' branches
}

_hydra_sessions() {
    local sessions
    sessions=(${(f)"$(command hydra list --json 2>/dev/null | tr '{' '\n' | sed -n 's/.*"branch": "\([^"]*\)".*/\1/p')"})
    _describe 'head' sessions
}

_hydra_templates() {
    local templates template_dir
    template_dir="${HYDRA_HOME:-$HOME/.hydra}/templates"
    if [[ -d "$template_dir" ]]; then
        templates=(${(f)"$(ls -1 "$template_dir"/*.yml "$template_dir"/*.yaml 2>/dev/null | xargs -n1 basename | sed 's/\.ya\?ml$//')"})
        _describe 'template' templates
    fi
}

_hydra "$@"
EOF
}
