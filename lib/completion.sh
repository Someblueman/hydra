#!/bin/sh
# Shell completion generation for Hydra
# POSIX-compliant shell script

# Generate bash completion script
# Usage: generate_bash_completion
# Returns: Bash completion script on stdout
generate_bash_completion() {
    cat <<'EOF'
# Bash completion for hydra
# Source this file or place it in /etc/bash_completion.d/

_hydra_completion() {
    local cur prev opts commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    commands="spawn init agent capabilities path lifecycle outcome wait adapter resume notify exec diff review provenance claim scope collision resource gate context sync land du gc worktree snapshot list switch kill regenerate state events status doctor dashboard dashboard-exit cycle-layout tui cleanup pr template completion version help group send recv tail broadcast wait-idle queue"
    opts="-h --help -v --version"
    
    case "${prev}" in
        hydra)
            COMPREPLY=($(compgen -W "${commands} ${opts}" -- ${cur}))
            return 0
            ;;
        spawn)
            # Complete with git branch names
            local branches=$(git branch 2>/dev/null | sed 's/^[ *]*//' | grep -v '^(')
            COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
            return 0
            ;;
        dashboard)
            case "${cur}" in
                --*)
                    COMPREPLY=($(compgen -W "--panes-per-session" -- ${cur}))
                    ;;
                *)
                    if [ "${prev}" = "--panes-per-session" ]; then
                        COMPREPLY=($(compgen -W "1 2 3 4 5 6 7 8 9 10 all" -- ${cur}))
                        return 0
                    fi
                    ;;
            esac
            return 0
            ;;
        claim) COMPREPLY=($(compgen -W "add list remove" -- ${cur})); return 0 ;;
        scope) COMPREPLY=($(compgen -W "set show check" -- ${cur})); return 0 ;;
        resource) COMPREPLY=($(compgen -W "allocate status env release" -- ${cur})); return 0 ;;
        gate) COMPREPLY=($(compgen -W "run approve status" -- ${cur})); return 0 ;;
        context) COMPREPLY=($(compgen -W "create" -- ${cur})); return 0 ;;
        worktree) COMPREPLY=($(compgen -W "doctor" -- ${cur})); return 0 ;;
        snapshot) COMPREPLY=($(compgen -W "--native --json" -- ${cur})); return 0 ;;
        tui) COMPREPLY=($(compgen -W "--basic --native --capabilities" -- ${cur})); return 0 ;;
        kill)
            # Complete with git branch names or --all flag
            case "${cur}" in
                -*)
                    COMPREPLY=($(compgen -W "--all --force --transcript" -- ${cur}))
                    ;;
                *)
                    local branches=$(git branch 2>/dev/null | sed 's/^[ *]*//' | grep -v '^(')
                    COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
                    ;;
            esac
            return 0
            ;;
        switch)
            # Complete with hydra session names from map file
            if [ -f "${HYDRA_MAP:-$HOME/.hydra/map}" ]; then
                local sessions=$(awk '{print $1}' "${HYDRA_MAP:-$HOME/.hydra/map}" 2>/dev/null)
                COMPREPLY=($(compgen -W "${sessions}" -- ${cur}))
            fi
            return 0
            ;;
        -l|--layout)
            # Complete with layout names
            COMPREPLY=($(compgen -W "default dev full" -- ${cur}))
            return 0
            ;;
        *)
            ;;
    esac
    
    # Check if we're completing a flag for spawn command
    if [[ "${COMP_WORDS[@]}" =~ spawn ]]; then
        case "${prev}" in
            -n|--count)
                # Complete with numbers 1-10
                COMPREPLY=($(compgen -W "1 2 3 4 5 6 7 8 9 10" -- ${cur}))
                return 0
                ;;
            --ai)
                # Complete with AI tools
                COMPREPLY=($(compgen -W "claude aider codex cursor copilot gemini" -- ${cur}))
                return 0
                ;;
            --agents)
                # Suggest example format
                COMPREPLY=($(compgen -W "claude:2,aider:1" -- ${cur}))
                return 0
                ;;
            -i|--issue)
                # GitHub issue numbers
                return 0
                ;;
            --pr)
                # GitHub PR numbers
                return 0
                ;;
            --after)
                # Complete with hydra session branches
                if [ -f "${HYDRA_MAP:-$HOME/.hydra/map}" ]; then
                    local branches=$(awk '{print $1}' "${HYDRA_MAP:-$HOME/.hydra/map}" 2>/dev/null)
                    COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
                fi
                return 0
                ;;
            --completion-policy)
                COMPREPLY=($(compgen -W "declared-done observed-exit-zero either" -- ${cur}))
                return 0
                ;;
        esac

        case "${cur}" in
            -*)
                COMPREPLY=($(compgen -W "-l --layout -n --count --ai --profile --no-agent --dry-run --prompt --prompt-file --issue-body --completion-policy --scope-read --scope-write --agents -i --issue --pr --pr-new --after -t --template" -- ${cur}))
                return 0
                ;;
        esac
    fi

    # Check if we're completing a flag for list command
    if [[ "${COMP_WORDS[@]}" =~ list ]]; then
        case "${cur}" in
            -*)
                COMPREPLY=($(compgen -W "--json --deps --git --no-pr-status --refresh-pr-status" -- ${cur}))
                return 0
                ;;
        esac
    fi

    if [[ "${COMP_WORDS[@]}" =~ exec ]]; then
        case "${cur}" in
            -*) COMPREPLY=($(compgen -W "--branch --group --all --jobs --timeout --json --shell --allow-shell" -- ${cur})); return 0 ;;
        esac
    fi

    if [[ "${COMP_WORDS[@]}" =~ claim ]]; then
        case "${prev}" in --access) COMPREPLY=($(compgen -W "read write" -- ${cur})); return 0 ;; esac
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--path --access --reason --expires-at --json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ scope ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--read --write --json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ collision ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ resource ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--port --compose-project --database --json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ gate ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--name --by --reason --json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ context ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--diff --file --note --history --artifact --json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ sync ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--from --gate --dry-run" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ land ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--into --gate --dry-run --keep-head" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ du ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ gc ]]; then
        case "${prev}" in --policy) COMPREPLY=($(compgen -W "orphaned stopped archives" -- ${cur})); return 0 ;; esac
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--policy --apply --dry-run --include-dirty --older-than" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ worktree ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--reason --dry-run --apply" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ snapshot ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--native --json" -- ${cur})); return 0 ;; esac
    fi

    # Check if we're completing template subcommand
    if [[ "${COMP_WORDS[@]}" =~ template ]]; then
        case "${prev}" in
        template)
                COMPREPLY=($(compgen -W "list create show edit delete" -- ${cur}))
            return 0
            ;;
        state)
            COMPREPLY=($(compgen -W "verify backup migrate rollback" -- ${cur}))
            return 0
            ;;
        events)
            COMPREPLY=($(compgen -W "verify tail filter retain repair" -- ${cur}))
            return 0
            ;;
        esac
    fi

    # Complete pr command with hydra session branches
    if [[ "${COMP_WORDS[@]}" =~ " pr" ]]; then
        if [ -f "${HYDRA_MAP:-$HOME/.hydra/map}" ]; then
            local branches=$(awk '{print $1}' "${HYDRA_MAP:-$HOME/.hydra/map}" 2>/dev/null)
            COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
        fi
        return 0
    fi
    
    # Check if we're completing a flag for kill command
    if [[ "${COMP_WORDS[@]}" =~ kill ]]; then
        case "${prev}" in
            --all)
                # After --all, only --force is valid
                COMPREPLY=($(compgen -W "--force" -- ${cur}))
                return 0
                ;;
        esac
    fi
}

complete -F _hydra_completion hydra
EOF
}

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
                sync)
                    _arguments '--from[Source ref]:ref:' '--gate[Approved gate]:name:' '--dry-run[Simulate without mutation]' '1:head:_hydra_sessions'
                    ;;
                land)
                    _arguments '--into[Current target branch]:branch:_hydra_branches' '--gate[Approved gate]:name:' '--dry-run[Simulate without mutation]' '--keep-head[Keep source head after landing]' '1:head:_hydra_sessions'
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
        'spawn:Create a new worktree and tmux session'
        'init:Initialize project identity, trust, profile, and worktree root'
        'agent:Manage agent profiles'
        'capabilities:Print machine-readable capabilities'
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
    if [[ -f "${HYDRA_MAP:-$HOME/.hydra/map}" ]]; then
        sessions=(${(f)"$(awk '{print $1}' "${HYDRA_MAP:-$HOME/.hydra/map}" 2>/dev/null)"})
        _describe 'session' sessions
    fi
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

# Generate fish completion script
# Usage: generate_fish_completion
# Returns: Fish completion script on stdout
generate_fish_completion() {
    cat <<'EOF'
# Fish completion for hydra

# Complete commands
complete -c hydra -f -n '__fish_use_subcommand' -a 'spawn' -d 'Create a new worktree and tmux session'
complete -c hydra -f -n '__fish_use_subcommand' -a 'init' -d 'Initialize project identity, trust, profile, and worktree root'
complete -c hydra -f -n '__fish_use_subcommand' -a 'agent' -d 'Manage agent profiles'
complete -c hydra -f -n '__fish_use_subcommand' -a 'capabilities' -d 'Print machine-readable capabilities'
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
complete -c hydra -f -n '__fish_seen_subcommand_from tui' -l native -d 'Use native mission control'
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

# Complete switch command with hydra sessions
complete -c hydra -f -n '__fish_seen_subcommand_from switch' -a '(test -f "$HYDRA_MAP"; or test -f "$HOME/.hydra/map"; and awk "{print \$1}" "$HYDRA_MAP" "$HOME/.hydra/map" 2>/dev/null)'

# Complete pr command with hydra sessions
complete -c hydra -f -n '__fish_seen_subcommand_from pr' -a '(test -f "$HYDRA_MAP"; or test -f "$HOME/.hydra/map"; and awk "{print \$1}" "$HYDRA_MAP" "$HOME/.hydra/map" 2>/dev/null)'
EOF
}

# Install completion scripts
# Usage: install_completions [bash|zsh|fish]
# Returns: 0 on success, 1 on failure
install_completions() {
    shell="${1:-all}"
    
    case "$shell" in
        bash|all)
            # Try to find bash completion directory
            if [ -d "/etc/bash_completion.d" ]; then
                comp_dir="/etc/bash_completion.d"
            elif [ -d "/usr/share/bash-completion/completions" ]; then
                comp_dir="/usr/share/bash-completion/completions"
            elif [ -d "/usr/local/etc/bash_completion.d" ]; then
                comp_dir="/usr/local/etc/bash_completion.d"
            else
                echo "Warning: No bash completion directory found" >&2
                echo "Generated completion saved to hydra-completion.bash" >&2
                generate_bash_completion > hydra-completion.bash
                return 0
            fi
            
            echo "Installing bash completion to $comp_dir/hydra"
            if generate_bash_completion > "$comp_dir/hydra" 2>/dev/null; then
                echo "Bash completion installed successfully"
            else
                echo "Error: Failed to install bash completion (permission denied?)" >&2
                echo "Try running with sudo or save manually:" >&2
                echo "  hydra completion bash > hydra-completion.bash" >&2
                return 1
            fi
            ;;
    esac
    
    case "$shell" in
        zsh|all)
            # Try to find zsh completion directory
            if [ -d "/usr/share/zsh/site-functions" ]; then
                comp_dir="/usr/share/zsh/site-functions"
            elif [ -d "/usr/local/share/zsh/site-functions" ]; then
                comp_dir="/usr/local/share/zsh/site-functions"
            else
                echo "Warning: No zsh completion directory found" >&2
                echo "Generated completion saved to _hydra" >&2
                generate_zsh_completion > _hydra
                return 0
            fi
            
            echo "Installing zsh completion to $comp_dir/_hydra"
            if generate_zsh_completion > "$comp_dir/_hydra" 2>/dev/null; then
                echo "Zsh completion installed successfully"
            else
                echo "Error: Failed to install zsh completion (permission denied?)" >&2
                echo "Try running with sudo or save manually:" >&2
                echo "  hydra completion zsh > _hydra" >&2
                return 1
            fi
            ;;
    esac
    
    case "$shell" in
        fish|all)
            # Try to find fish completion directory
            if [ -d "$HOME/.config/fish/completions" ]; then
                comp_dir="$HOME/.config/fish/completions"
            elif [ -d "/usr/share/fish/completions" ]; then
                comp_dir="/usr/share/fish/completions"
            elif [ -d "/usr/local/share/fish/completions" ]; then
                comp_dir="/usr/local/share/fish/completions"
            else
                echo "Warning: No fish completion directory found" >&2
                echo "Generated completion saved to hydra.fish" >&2
                generate_fish_completion > hydra.fish
                return 0
            fi
            
            echo "Installing fish completion to $comp_dir/hydra.fish"
            if generate_fish_completion > "$comp_dir/hydra.fish" 2>/dev/null; then
                echo "Fish completion installed successfully"
            else
                echo "Error: Failed to install fish completion (permission denied?)" >&2
                echo "Try saving manually:" >&2
                echo "  hydra completion fish > hydra.fish" >&2
                return 1
            fi
            ;;
    esac
    
    return 0
}

# Generate completion script for specified shell
# Usage: generate_completion <shell>
# shell: bash, zsh, or fish (default: bash)
# Returns: Completion script on stdout, 1 on unknown shell
generate_completion() {
    shell="${1:-bash}"

    case "$shell" in
        bash)
            generate_bash_completion
            ;;
        zsh)
            generate_zsh_completion
            ;;
        fish)
            generate_fish_completion
            ;;
        *)
            echo "Error: Unknown shell '$shell'" >&2
            echo "Supported shells: bash, zsh, fish" >&2
            return 1
            ;;
    esac
}
