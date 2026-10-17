#!/bin/sh
# Bash completion generator for Hydra.

# Generate bash completion script
# Usage: generate_bash_completion
# Returns: Bash completion script on stdout
generate_bash_completion() {
    cat <<'EOF'
# Bash completion for hydra
# Source this file or place it in /etc/bash_completion.d/

_hydra_active_heads() {
    command hydra list --json 2>/dev/null | tr '{' '\n' | sed -n 's/.*"branch": "\([^"]*\)".*/\1/p'
}

_hydra_completion() {
    local cur prev opts commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    if [[ ${COMP_WORDS[1]:-} == kill && $COMP_CWORD -gt 2 ]]; then
        case "$prev" in -g|--group) return 0 ;; esac
        if [[ -z $cur || $cur == -* ]]; then
            COMPREPLY=($(compgen -W "--all --dry-run --force -g --group --protect-untracked --transcript" -- "$cur"))
            return 0
        fi
    fi

    commands="remote fleet admission spawn init agent capabilities workflow path lifecycle outcome wait adapter resume notify exec diff review provenance claim scope collision resource gate context sync land integrate du gc worktree snapshot list switch kill regenerate state events status doctor dashboard dashboard-exit cycle-layout tui cleanup pr template completion version help group send recv tail broadcast wait-idle queue pool schedule"
    opts="-h --help -v --version"
    if [[ ${COMP_WORDS[1]:-} == remote && $COMP_CWORD -ge 3 && ( -z $cur || $cur == -* ) ]]; then
        case ${COMP_WORDS[2]:-} in
            setup) COMPREPLY=($(compgen -W "status list remove --ssh-config --binary --approve --timeout --json" -- "${cur}")); return 0 ;;
            trust-key) COMPREPLY=($(compgen -W "--fingerprint --timeout --json" -- "${cur}")); return 0 ;;
            preflight) COMPREPLY=($(compgen -W "--timeout --json" -- "${cur}")); return 0 ;;
            provision) COMPREPLY=($(compgen -W "--approve --binary --timeout --json" -- "${cur}")); return 0 ;;
            agents) COMPREPLY=($(compgen -W "--record --timeout --json" -- "${cur}")); return 0 ;;
            install-agent) COMPREPLY=($(compgen -W "--agent --approve --timeout --json" -- "${cur}")); return 0 ;;
            sign-in) COMPREPLY=($(compgen -W "--agent --timeout --json" -- "${cur}")); return 0 ;;
        esac
    fi
    if [[ ${COMP_WORDS[1]:-} == fleet && ${COMP_WORDS[2]:-} == task && ${COMP_WORDS[3]:-} == submit && ${prev} == --placement ]]; then
        COMPREPLY=($(compgen -W "auto" -- "${cur}")); return 0
    fi
    if [[ ${COMP_WORDS[1]:-} == fleet && ${COMP_WORDS[2]:-} == placement && $COMP_CWORD -ge 4 ]]; then
        COMPREPLY=($(compgen -W "--json --timeout" -- "${cur}")); return 0
    if [[ ${COMP_WORDS[1]:-} == pool && $COMP_CWORD -ge 3 && ( -z $cur || $cur == -* ) ]]; then
        case ${COMP_WORDS[2]:-} in
            create) COMPREPLY=($(compgen -W "--max-tasks --max-outstanding --max-expansions --max-artifact-bytes --max-repair-rounds --lease --retry --json" -- "${cur}")); return 0 ;;
            add) COMPREPLY=($(compgen -W "--workflow --exec --id --idempotent --retry --seal --json" -- "${cur}")); return 0 ;;
            claim) COMPREPLY=($(compgen -W "--worker --owner-pid --lease --json" -- "${cur}")); return 0 ;;
            complete) COMPREPLY=($(compgen -W "--token --by --outcome --run --json" -- "${cur}")); return 0 ;;
            run) COMPREPLY=($(compgen -W "--worker --max --lease --json" -- "${cur}")); return 0 ;;
            seal|cancel|status|list) COMPREPLY=($(compgen -W "--json" -- "${cur}")); return 0 ;;
        esac
    fi
    if [[ ${COMP_WORDS[1]:-} == schedule && $COMP_CWORD -ge 3 && ( -z $cur || $cur == -* ) ]]; then
        case ${COMP_WORDS[2]:-} in
            add) COMPREPLY=($(compgen -W "--every --workflow --pool --missed --catch-up-limit --reboot-recovery --format --json" -- "${cur}")); return 0 ;;
            run-now) COMPREPLY=($(compgen -W "--token --json" -- "${cur}")); return 0 ;;
            status) COMPREPLY=($(compgen -W "--format --json" -- "${cur}")); return 0 ;;
            list|remove) COMPREPLY=($(compgen -W "--json" -- "${cur}")); return 0 ;;
        esac
    fi
    if [[ ${COMP_WORDS[1]:-} == fleet && ${COMP_WORDS[2]:-} == task && $COMP_CWORD -ge 5 ]]; then
        case ${COMP_WORDS[3]:-} in
            submit) COMPREPLY=($(compgen -W "--input --key --trust-spec --placement --separate-from" -- "${cur}")); return 0 ;;
            withdraw) COMPREPLY=($(compgen -W "--id --timeout" -- "${cur}")); return 0 ;;
            decide) COMPREPLY=($(compgen -W "--id --trust-spec --request --decision --by" -- "${cur}")); return 0 ;;
            start|resume) COMPREPLY=($(compgen -W "--id --trust-spec" -- "${cur}")); return 0 ;;
            collect) COMPREPLY=($(compgen -W "--id --input --into --timeout" -- "${cur}")); return 0 ;;
            collected) COMPREPLY=($(compgen -W "--id --into --format" -- "${cur}")); return 0 ;;
            result) COMPREPLY=($(compgen -W "--id --output --timeout" -- "${cur}")); return 0 ;;
            logs) COMPREPLY=($(compgen -W "--id --source --stream --offset --limit --step --attempt" -- "${cur}")); return 0 ;;
            cancel) COMPREPLY=($(compgen -W "--id --timeout" -- "${cur}")); return 0 ;;
            status|observe) COMPREPLY=($(compgen -W "--id" -- "${cur}")); return 0 ;;
        esac
    fi
    
    case "${prev}" in
        hydra)
            COMPREPLY=($(compgen -W "${commands} ${opts}" -- ${cur}))
            return 0
            ;;
        remote)
            COMPREPLY=($(compgen -W "add remove list setup trust-key preflight provision agents install-agent sign-in" -- "${cur}"))
            return 0
            ;;
        agent)
            COMPREPLY=($(compgen -W "list show doctor init locate contract probe import" -- "${cur}"))
            return 0
            ;;
        locate)
            COMPREPLY=($(compgen -W "--json --record --forget" -- "${cur}"))
            return 0
            ;;
        fleet)
            COMPREPLY=($(compgen -W "handshake discover qualify enroll revoke list overview doctor admission bootstrap package task auth placement init spawn signal cancel workflow attach export import reconcile watch tui --json --jobs --timeout --project --instance --input --output --sha256" -- "${cur}"))
            return 0
            ;;
        discover|qualify)
            COMPREPLY=($(compgen -W "--ssh --inventory --select --source --diff --ssh-config --timeout --require --progress --json" -- "${cur}"))
            return 0
            ;;
        auth)
            COMPREPLY=($(compgen -W "status preview copy login help --agent --provider --source --approve --executable" -- "${cur}"))
            return 0
            ;;
        enroll)
            COMPREPLY=($(compgen -W "review apply status --input --output --candidate --project --package --sha256 --prefix --alias --confirm --timeout --json" -- "${cur}"))
            return 0
            ;;
        revoke)
            COMPREPLY=($(compgen -W "candidate source transport package project list --reason --scope --json" -- "${cur}"))
            return 0
            ;;
        task)
            COMPREPLY=($(compgen -W "prepare inspect announce submit start resume requests decide status observe cancel withdraw logs result inspect-result collect collected help" -- "${cur}"))
            return 0
            ;;
        placement)
            COMPREPLY=($(compgen -W "explain replay withdraw drain undrain policy help" -- "${cur}"))
            return 0
            ;;
        prepare)
            COMPREPLY=($(compgen -W "--source --spec --output" -- "${cur}"))
            return 0
            ;;
        inspect|inspect-result|announce)
            COMPREPLY=($(compgen -W "--input" -- "${cur}"))
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
        admission) COMPREPLY=($(compgen -W "status configure request inspect claim cancel unknown release --json --summary --confirmed" -- "${cur}")); return 0 ;;
        claim) COMPREPLY=($(compgen -W "add list remove" -- ${cur})); return 0 ;;
        scope) COMPREPLY=($(compgen -W "set show check" -- ${cur})); return 0 ;;
        resource) COMPREPLY=($(compgen -W "allocate status env release" -- ${cur})); return 0 ;;
        gate) COMPREPLY=($(compgen -W "run approve status" -- ${cur})); return 0 ;;
        context) COMPREPLY=($(compgen -W "create" -- ${cur})); return 0 ;;
        workflow) COMPREPLY=($(compgen -W "list show validate dry-run run status cancel resume replay requests decide statistics-data statistics-json statistics-compare plan" -- ${cur})); return 0 ;;
        pool) COMPREPLY=($(compgen -W "create add seal claim complete cancel status list run --json" -- ${cur})); return 0 ;;
        schedule) COMPREPLY=($(compgen -W "add list remove run-now status --json" -- ${cur})); return 0 ;;
        integrate) COMPREPLY=($(compgen -W "train status report cancel resume approve promote cleanup" -- ${cur})); return 0 ;;
        worktree) COMPREPLY=($(compgen -W "doctor" -- ${cur})); return 0 ;;
        snapshot) COMPREPLY=($(compgen -W "--native --json" -- ${cur})); return 0 ;;
        tui) COMPREPLY=($(compgen -W "--basic --capabilities" -- ${cur})); return 0 ;;
        kill)
            # Complete with git branch names or kill options
            case "${cur}" in
                -*)
                    COMPREPLY=($(compgen -W "--all --dry-run --force -g --group --protect-untracked --transcript" -- ${cur}))
                    ;;
                *)
                    local branches=$(git branch 2>/dev/null | sed 's/^[ *]*//' | grep -v '^(')
                    COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
                    ;;
            esac
            return 0
            ;;
        switch)
            local sessions=$(_hydra_active_heads)
            COMPREPLY=($(compgen -W "${sessions}" -- ${cur}))
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
                COMPREPLY=($(compgen -W "claude aider codex cursor agy opencode copilot gemini" -- ${cur}))
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
                local branches=$(_hydra_active_heads)
                COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
                return 0
                ;;
            --completion-policy)
                COMPREPLY=($(compgen -W "declared-done observed-exit-zero either" -- ${cur}))
                return 0
                ;;
        esac

        case "${cur}" in
            -*)
                COMPREPLY=($(compgen -W "-l --layout -n --count --ai --profile --no-agent --headless --dry-run --prompt --prompt-file --issue-body --completion-policy --scope-read --scope-write --agents -i --issue --pr --pr-new --after -t --template --attach --resume" -- ${cur}))
                return 0
                ;;
        esac
    fi

    # Check if we're completing a flag for list command
    if [[ "${COMP_WORDS[@]}" =~ list ]]; then
        case "${cur}" in
            -*)
                COMPREPLY=($(compgen -W "--json --verbose --deps --git --no-pr-status --refresh-pr-status" -- ${cur}))
                return 0
                ;;
        esac
    fi

    if [[ "${COMP_WORDS[@]}" =~ exec ]]; then
        case "${cur}" in
            -*) COMPREPLY=($(compgen -W "--branch --group --all --jobs --timeout --json --exit-code --profile --prompt-file --resume-run --result-file --require --retain-raw --shell --allow-shell" -- ${cur})); return 0 ;;
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
        if [[ "${prev}" == plan ]]; then COMPREPLY=($(compgen -W "schema propose proposal validate compile show obligations checks explain compare evaluate observed decision expand run result" -- "${cur}")); return 0; fi
        if [[ "${COMP_WORDS[@]}" =~ plan && "${cur}" == -* ]]; then COMPREPLY=($(compgen -W "--accept --json --branch --local-policy --return --asset --assets-dir --run --proposal" -- "${cur}")); return 0; fi
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
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--policy --apply --dry-run --include-dirty --older-than --path" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ worktree ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--reason --dry-run --apply" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[@]}" =~ snapshot ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--native --json" -- ${cur})); return 0 ;; esac
    fi
    if [[ "${COMP_WORDS[1]}" == doctor ]]; then
        case "${cur}" in -*) COMPREPLY=($(compgen -W "--fix --fix-permissions" -- ${cur})); return 0 ;; esac
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
        local branches=$(_hydra_active_heads)
        COMPREPLY=($(compgen -W "${branches}" -- ${cur}))
        return 0
    fi
    
}

complete -F _hydra_completion hydra
EOF
}
