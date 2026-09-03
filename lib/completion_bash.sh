#!/bin/sh
# Bash completion generator for Hydra.

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

    commands="spawn init agent capabilities workflow path lifecycle outcome wait adapter resume notify exec diff review provenance claim scope collision resource gate context sync land integrate du gc worktree snapshot list switch kill regenerate state events status doctor dashboard dashboard-exit cycle-layout tui cleanup pr template completion version help group send recv tail broadcast wait-idle queue"
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
        workflow) COMPREPLY=($(compgen -W "list show validate dry-run run status cancel resume" -- ${cur})); return 0 ;;
        integrate) COMPREPLY=($(compgen -W "train status report cancel resume approve promote cleanup" -- ${cur})); return 0 ;;
        worktree) COMPREPLY=($(compgen -W "doctor" -- ${cur})); return 0 ;;
        snapshot) COMPREPLY=($(compgen -W "--native --json" -- ${cur})); return 0 ;;
        tui) COMPREPLY=($(compgen -W "--basic --capabilities" -- ${cur})); return 0 ;;
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
    if [[ "${COMP_WORDS[@]}" =~ workflow ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ integrate ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--base --target --into --dry-run --execute --gate --by --apply" -- ${cur})); return 0 ;; esac
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

