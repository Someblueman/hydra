#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

# Parser owns the command variables below; subsequent phases consume them in order.
spawn_parse_options() {
    # Parse arguments
    branch=""
    layout="default"
    count=1
    ai_tool=""
    explicit_profile=""
    agents_spec=""
    issue_num=""
    group=""
    after_deps=""
    pr_num=""
    pr_new=""
    template_name=""
    no_agent=""
    headless=""
    dry_run=""
    task_text=""
    task_source=""
    prompt_file=""
    use_issue_body=""
    completion_policy=declared-done
    scope_rules=""
    attach=""
    resume_existing=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -l|--layout)
                shift
                layout="$1"
                shift
                ;;
            -n|--count)
                shift
                count="$1"
                shift
                ;;
            --attach)
                # Direct terminal attachment is an explicit expert choice; the
                # interactive default keeps the user in the control centre.
                attach="1"
                shift
                ;;
            --resume)
                resume_existing="1"
                shift
                ;;
            --ai|--profile)
                [ $# -ge 2 ] || { echo "Error: $1 requires a profile name" >&2; exit 1; }
                ai_tool="$2"
                explicit_profile="$2"
                shift 2
                ;;
            --no-agent)
                no_agent="1"
                shift
                ;;
            --headless)
                headless="1"
                shift
                ;;
            --dry-run)
                dry_run="1"
                shift
                ;;
            --prompt)
                [ $# -ge 2 ] || { echo "Error: --prompt requires task text" >&2; exit 1; }
                [ -z "$task_source" ] || { echo "Error: choose only one task source" >&2; exit 1; }
                task_text="$2"
                task_source="prompt"
                shift 2
                ;;
            --prompt-file)
                [ $# -ge 2 ] || { echo "Error: --prompt-file requires a path" >&2; exit 1; }
                [ -z "$task_source" ] || { echo "Error: choose only one task source" >&2; exit 1; }
                prompt_file="$2"
                task_source="file"
                shift 2
                ;;
            --issue-body)
                [ -z "$task_source" ] || { echo "Error: choose only one task source" >&2; exit 1; }
                use_issue_body="1"
                task_source="issue"
                shift
                ;;
            --completion-policy)
                [ $# -ge 2 ] || { echo "Error: --completion-policy requires declared-done, observed-exit-zero, or either" >&2; exit 1; }
                completion_policy="$2"
                case "$completion_policy" in declared-done|observed-exit-zero|either) ;; *) echo "Error: invalid completion policy '$completion_policy'" >&2; exit 1 ;; esac
                shift 2
                ;;
            --scope-read|--scope-write)
                [ $# -ge 2 ] || { echo "Error: $1 requires a repository-relative pattern" >&2; exit 1; }
                _csp_mode="${1#--scope-}"
                parallel_validate_path_pattern "$2" || { echo "Error: invalid scope pattern '$2'" >&2; exit 1; }
                _csp_tab="$(printf '\t')"
                if [ -n "$scope_rules" ]; then
                    scope_rules="$scope_rules
$_csp_mode$_csp_tab$2"
                else
                    scope_rules="$_csp_mode$_csp_tab$2"
                fi
                shift 2
                ;;
            --agents)
                shift
                agents_spec="$1"
                shift
                ;;
            -i|--issue)
                shift
                issue_num="$1"
                shift
                ;;
            -g|--group)
                shift
                group="$1"
                shift
                ;;
            --after)
                shift
                after_deps="$1"
                shift
                ;;
            --pr)
                shift
                pr_num="$1"
                shift
                ;;
            --pr-new)
                pr_new="1"
                shift
                ;;
            -t|--template)
                shift
                template_name="$1"
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra spawn <branch> [-l|--layout <layout>] [-n|--count <number>] [--ai <tool>] [--attach] [--resume] [--agents <spec>] [-g|--group <name>] [--after <deps>] [-t|--template <name>]" >&2
                echo "       hydra spawn --issue <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                echo "       hydra spawn --pr <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                exit 1
                ;;
            *)
                if [ -z "$branch" ]; then
                    branch="$1"
                else
                    echo "Error: Too many arguments" >&2
                    echo "Usage: hydra spawn <branch> [-l|--layout <layout>] [-n|--count <number>] [--ai <tool>] [--attach] [--resume] [--agents <spec>] [-g|--group <name>] [--after <deps>] [-t|--template <name>]" >&2
                    echo "       hydra spawn --issue <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                    echo "       hydra spawn --pr <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    return 0
}

cmd_spawn() {
    spawn_parse_options "$@" || return $?
    # Handle GitHub issue mode
    if [ -n "$issue_num" ]; then
        if [ -n "$branch" ]; then
            echo "Error: Cannot specify both branch name and issue number" >&2
            exit 1
        fi

        # Check for incompatible options
        if [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; then
            echo "Error: Cannot use bulk spawn options with --issue" >&2
            exit 1
        fi

        if [ -n "$pr_num" ]; then
            echo "Error: Cannot specify both --issue and --pr" >&2
            exit 1
        fi

        # Generate branch from issue
        branch="$(spawn_from_issue "$issue_num")" || exit 1
    fi

    # Handle GitHub PR mode
    if [ -n "$pr_num" ]; then
        if [ -n "$branch" ]; then
            echo "Error: Cannot specify both branch name and --pr" >&2
            exit 1
        fi

        # Check for incompatible options
        if [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; then
            echo "Error: Cannot use bulk spawn options with --pr" >&2
            exit 1
        fi

        # Get branch from PR
        _load_lib github
        branch="$(spawn_from_pr "$pr_num")" || exit 1
    fi

    if [ -z "$branch" ]; then
        echo "Error: Branch name is required" >&2
        echo "Usage: hydra spawn <branch> [-l|--layout <layout>] [-n|--count <number>] [--ai <tool>] [--attach] [--resume] [--agents <spec>] [-g|--group <name>]" >&2
        echo "       hydra spawn --issue <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
        echo "       hydra spawn --pr <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
        exit 1
    fi

    if [ -n "$no_agent" ] && { [ -n "$explicit_profile" ] || [ -n "$agents_spec" ]; }; then
        echo "Error: --no-agent cannot be combined with --profile/--ai" >&2
        exit 1
    fi
    if [ -n "$headless" ] && [ -n "$explicit_profile" ] && [ "$explicit_profile" != none ] && [ -z "$no_agent" ]; then
        echo "Error: --headless cannot launch an interactive profile; use hydra exec --profile with a headless adapter" >&2
        exit 1
    fi
    if [ -n "$headless" ] && [ -z "$explicit_profile" ]; then
        # Headless spawn is a workspace/execution identity operation. Adapter
        # invocation is a separate `hydra exec --profile` contract.
        no_agent=1
    fi
    if [ -z "$agents_spec" ]; then
        if [ -n "$no_agent" ] || [ -n "${HYDRA_SKIP_AI:-}" ]; then
            ai_tool=none
            HYDRA_SKIP_AI=1
            export HYDRA_SKIP_AI
        fi
        ai_tool="$(profile_resolve "$ai_tool")" || exit 1
        if [ "$ai_tool" = none ]; then
            HYDRA_SKIP_AI=1
            export HYDRA_SKIP_AI
        fi
    fi

    if [ -n "$prompt_file" ]; then
        if [ ! -f "$prompt_file" ] || [ ! -r "$prompt_file" ]; then
            echo "Error: prompt file is not readable: $prompt_file" >&2
            exit 1
        fi
        task_bytes="$(LC_ALL=C wc -c < "$prompt_file" | tr -d ' ')"
        [ "$task_bytes" -le 65536 ] || { echo "Error: task input exceeds 65536 bytes" >&2; exit 1; }
        task_text="$(sed -n '1,$p' "$prompt_file")"
    fi
    if [ -n "$use_issue_body" ]; then
        [ -n "$issue_num" ] || { echo "Error: --issue-body requires --issue <number>" >&2; exit 1; }
        task_text="$(get_issue_body "$issue_num")" || exit 1
    fi
    if [ -n "$scope_rules" ]; then
        _csp_instructions="Hydra scope (coordination guidance, not a security boundary):"
        _csp_tab="$(printf '\t')"
        while IFS="$_csp_tab" read -r _csp_mode _csp_pattern; do
            _csp_instructions="$_csp_instructions
- $_csp_mode: $_csp_pattern"
        done <<EOF
$scope_rules
EOF
        _csp_instructions="$_csp_instructions
Do not modify read-only or out-of-scope paths. Before declaring completion, run: hydra scope check $branch"
        if [ -n "$task_text" ]; then
            task_text="$task_text

$_csp_instructions"
        else
            task_text="$_csp_instructions"
        fi
    fi
    task_bytes="$(printf '%s' "$task_text" | LC_ALL=C wc -c | tr -d ' ')"
    [ "$task_bytes" -le 65536 ] || { echo "Error: task input exceeds 65536 bytes" >&2; exit 1; }

    # Validate count
    if ! echo "$count" | grep -q '^[0-9]\+$' || [ "$count" -lt 1 ] || [ "$count" -gt 10 ]; then
        echo "Error: Count must be a number between 1 and 10" >&2
        exit 1
    fi
    if [ -n "$headless" ] && { [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; }; then
        echo "Error: --headless currently requires a single head" >&2
        exit 1
    fi
    if [ -n "$attach" ] && [ -n "$headless" ]; then
        echo "Error: --attach cannot be combined with --headless (a headless head has no terminal)" >&2
        exit 1
    fi
    if { [ -n "$attach" ] || [ -n "$resume_existing" ]; } && { [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; }; then
        echo "Error: --attach and --resume require a single head" >&2
        exit 1
    fi
    if [ -n "$resume_existing" ] && { [ -n "$task_source" ] || [ -n "$scope_rules" ] || [ -n "$template_name" ] || [ -n "$pr_new" ] || [ -n "$after_deps" ]; }; then
        echo "Error: --resume reuses the head's recorded task and metadata; it cannot be combined with --prompt, --prompt-file, --issue-body, --scope-*, --template, --pr-new, or --after" >&2
        exit 1
    fi

    if { [ -n "$task_source" ] || [ -n "$dry_run" ]; } && \
       { [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; }; then
        echo "Error: task injection and dry-run currently require a single head" >&2
        exit 1
    fi
    if [ -n "$scope_rules" ] && { [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; }; then
        echo "Error: scoped spawn currently requires a single head" >&2
        exit 1
    fi

    # Validate layout early to avoid injection in apply step
    case "$layout" in
        default|dev|full) ;;
        *)
            echo "Error: Invalid layout '$layout' (allowed: default, dev, full)" >&2
            exit 1
            ;;
    esac

    # Validate template if specified
    if [ -n "$template_name" ]; then
        _load_lib template
        if ! template_exists "$template_name"; then
            echo "Error: Template '$template_name' not found" >&2
            templates="$(list_templates)"
            if [ -n "$templates" ]; then
                echo "Available templates: $(echo "$templates" | tr '\n' ' ')" >&2
            else
                echo "No templates available. Create one with: hydra template create <name>" >&2
            fi
            exit 1
        fi
    fi

    # Handle mutually exclusive options
    if [ -n "$agents_spec" ] && [ -n "$explicit_profile" ]; then
        echo "Error: Cannot use both --ai and --agents options" >&2
        exit 1
    fi

    # Validate --after (dependencies) if specified
    if [ -n "$after_deps" ]; then
        # Can't use --after with bulk spawn
        if [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; then
            echo "Error: Cannot use --after with bulk spawn options" >&2
            exit 1
        fi

        # Load deps library and validate
        _load_lib deps
        if ! validate_deps_spec "$after_deps"; then
            exit 1
        fi

        # Check for circular dependencies
        if ! check_circular_deps "$branch" "$after_deps"; then
            exit 1
        fi
    fi

    # Durable head state outlives `hydra kill`; a branch that already has a
    # head must be resumed or renamed. Dry-run and the actual spawn share this
    # check so the plan never claims a spawn that would be refused.
    if [ "$count" -eq 1 ] && [ -z "$agents_spec" ]; then
        if spawn_existing_head_status "$branch"; then
            if [ -n "$resume_existing" ] && [ "$SPAWN_EXISTING_LIVENESS" != live ]; then
                spawn_resume_existing "$branch" "$dry_run" || return 1
                return 0
            fi
            spawn_report_existing_head "$branch"
            exit 1
        fi
    fi

    if [ -n "$dry_run" ]; then
        _dry_terminal_mode=interactive
        [ -z "$headless" ] || _dry_terminal_mode=headless
        spawn_dry_run "$branch" "$layout" "$ai_tool" "$group" "$after_deps" "$pr_num" "$template_name" "$task_text" "$completion_policy" "$scope_rules" "$_dry_terminal_mode"
        return $?
    fi

    # Check resource limits before spawning
    if is_limit_enabled; then
        # Calculate total sessions to spawn
        total_to_spawn="$count"
        if [ -n "$agents_spec" ]; then
            # Count total from agents spec (e.g., "claude:2,aider:1" = 3)
            total_to_spawn=0
            # Parse spec and sum counts
            _spec="$agents_spec"
            while [ -n "$_spec" ]; do
                _item="${_spec%%,*}"
                _agent_count="${_item#*:}"
                if [ "$_agent_count" != "$_item" ]; then
                    total_to_spawn=$((total_to_spawn + _agent_count))
                else
                    total_to_spawn=$((total_to_spawn + 1))
                fi
                if [ "$_spec" = "$_item" ]; then
                    break
                fi
                _spec="${_spec#*,}"
            done
        fi

        if would_exceed_limit "$total_to_spawn"; then
            max="$(get_max_sessions)"
            current="$(get_active_session_count)"
            available="$(get_available_capacity)"

            echo "Session limit reached: $current/$max active sessions" >&2

            if [ "$available" -gt 0 ] && [ "$available" -lt "$total_to_spawn" ]; then
                echo "Can only spawn $available of requested $total_to_spawn sessions" >&2
            fi

            # Prompt to queue spawns (only in interactive mode)
            if [ -t 0 ] && [ -t 1 ]; then
                printf "Queue spawn request(s) for later? [y/N] "
                read -r response
                case "$response" in
                    [yY][eE][sS]|[yY])
                        # Queue the spawn(s)
                        if [ -n "$agents_spec" ]; then
                            queue_mixed_spawns "$branch" "$agents_spec" "$group" "$layout"
                            echo "Queued $total_to_spawn spawn(s). Run 'hydra queue' to view pending."
                        elif [ "$total_to_spawn" -eq 1 ]; then
                            queue_spawn "$branch" "$ai_tool" "$group" "$layout" "50" >/dev/null
                            echo "Queued spawn for '$branch'. Run 'hydra queue' to view pending."
                        else
                            queue_bulk_spawns "$branch" "$total_to_spawn" "$ai_tool" "$group" "$layout"
                            echo "Queued $total_to_spawn spawn(s). Run 'hydra queue' to view pending."
                        fi
                        return 0
                        ;;
                    *)
                        echo "Aborted" >&2
                        exit 1
                        ;;
                esac
            else
                echo "Error: Cannot spawn - session limit reached (non-interactive mode)" >&2
                exit 1
            fi
        fi
    fi

    # If agents spec is provided, delegate to bulk spawn with mixed agents
    if [ -n "$agents_spec" ]; then
        spawn_bulk_mixed "$branch" "$agents_spec" "$layout" "$group"
        return $?
    fi

    # If count > 1, delegate to bulk spawn
    if [ "$count" -gt 1 ]; then
        spawn_bulk "$branch" "$count" "$layout" "$ai_tool" "$group"
        return $?
    fi

    # Single spawn - use helper function
    # Pass pr_num if spawning from PR (to store in state)
    spawn_pr_num=""
    if [ -n "$pr_num" ]; then
        spawn_pr_num="$pr_num"
    fi

    _terminal_mode=interactive
    [ -z "$headless" ] || _terminal_mode=headless
    if session="$(spawn_single "$branch" "$layout" "$ai_tool" "$group" "$after_deps" "$spawn_pr_num" "$template_name" "$task_text" "$completion_policy" "$scope_rules" "$_terminal_mode")"; then
        # Handle --pr-new: create a draft PR after spawn
        if [ -n "$pr_new" ]; then
            _load_lib github
            echo "Creating draft PR for branch '$branch'..." >&2
            new_pr="$(create_pr_for_branch "$branch" --draft 2>&1)" || {
                echo "Warning: Failed to create PR: $new_pr" >&2
            }
            if [ -n "$new_pr" ] && echo "$new_pr" | grep -q '^[0-9]*$'; then
                set_pr_for_branch "$branch" "$new_pr"
                echo "Created draft PR #$new_pr" >&2
            fi
        fi

        spawn_finish_launch "$branch" "$session" "$ai_tool" "$_terminal_mode" created || return 1
        return 0
    else
        return 1
    fi
}

# Decide what happens to the user's terminal after a head is launched.
# Usage: spawn_finish_launch <branch> <session> <profile> <terminal_mode> <verb>
# Consumes the parser's `attach` variable. Documented noninteractive contracts
# (HYDRA_NO_SWITCH, non-TTY, headless) keep their exact messages; the
# interactive default opens the head inside the native control centre, or
# stays in the current terminal when the optional native TUI is unavailable.
spawn_finish_launch() {
    _sfl_branch="$1"
    _sfl_session="$2"
    _sfl_profile="$3"
    _sfl_mode="$4"
    _sfl_verb="${5:-created}"
    if [ "$_sfl_mode" = headless ]; then
        echo "Headless head '$_sfl_branch' $_sfl_verb (no terminal; use hydra exec --branch $_sfl_branch -- ...)"
    elif [ -n "${HYDRA_NO_SWITCH:-}" ]; then
        echo "Session '$_sfl_session' $_sfl_verb (HYDRA_NO_SWITCH set; not attaching)"
        [ -z "${attach:-}" ] || echo "Note: --attach ignored because HYDRA_NO_SWITCH is set" >&2
    elif [ -t 0 ] && [ -t 1 ]; then
        if [ -n "${attach:-}" ]; then
            # Explicit expert choice: take over this terminal. A failed
            # attach keeps its nonzero status but still tells the user where
            # the running head is.
            echo "Switching to session '$_sfl_session'..."
            switch_to_session "$_sfl_session" || {
                echo "Could not attach to session '$_sfl_session'; the head is still running." >&2
                spawn_print_context "$_sfl_branch" "$_sfl_session" "$_sfl_profile" "$_sfl_verb"
                return 1
            }
        else
            spawn_print_context "$_sfl_branch" "$_sfl_session" "$_sfl_profile" "$_sfl_verb"
            # Task input lives in the optional native workspace. Without it,
            # stay in this terminal: the basic TUI cannot open the new task.
            _load_libs_for_cmd tui
            if tui_native_find_binary >/dev/null 2>&1; then
                cmd_tui --task "$_sfl_branch"
                return $?
            fi
        fi
    else
        echo "Session '$_sfl_session' $_sfl_verb successfully (not switching - not in terminal)"
    fi
}

# Print the concise, human-readable launch context for an interactive head.
# Usage: spawn_print_context <branch> <session> <profile> <verb>
spawn_print_context() {
    _spc_branch="$1"
    _spc_session="$2"
    _spc_profile="$3"
    _spc_verb="${4:-created}"
    _spc_worktree="$(spawn_worktree_for_branch "$_spc_branch" 2>/dev/null || true)"
    _spc_agent="$_spc_profile"
    case "$_spc_agent" in ''|none|-) _spc_agent="none (plain shell)" ;; esac
    echo "Head '$_spc_branch' $_spc_verb; it keeps running in the background."
    echo "  branch:   $_spc_branch"
    echo "  agent:    $_spc_agent"
    [ -z "$_spc_worktree" ] || echo "  worktree: $(spawn_human_path "$_spc_worktree")"
    echo "  session:  $_spc_session (tmux)"
    echo "Next:"
    echo "  hydra tui                   follow it in the control centre"
    echo "  hydra switch $_spc_branch   attach to its terminal directly"
}

# Abbreviate the home directory for display only; stored paths stay absolute.
# Usage: spawn_human_path <path>
spawn_human_path() {
    case "${HOME:-}" in
        ''|/) printf '%s\n' "$1" ;;
        *)
            case "$1" in
                "$HOME"/*) printf '~%s\n' "${1#"$HOME"}" ;;
                *) printf '%s\n' "$1" ;;
            esac
            ;;
    esac
}

# Resolve the worktree Hydra recorded for a branch's head.
# Usage: spawn_worktree_for_branch <branch>
spawn_worktree_for_branch() {
    _swfb_project="$(hydra_get_project_id 2>/dev/null)" || return 1
    _swfb_head="$(state_v2_find_head_by_branch "$_swfb_project" "$1" 2>/dev/null)" || return 1
    _swfb_dir="$(state_v2_head_dir "$_swfb_project" "$_swfb_head")" || return 1
    _swfb_stored="$(sed -n '1p' "$_swfb_dir/worktree" 2>/dev/null || true)"
    if [ -n "$_swfb_stored" ]; then
        printf '%s\n' "$_swfb_stored"
    else
        project_worktree_path "$_swfb_project" "$_swfb_head"
    fi
}

# Detect durable head state for a branch without mutating anything.
# Usage: spawn_existing_head_status <branch>
# Returns 0 when a head exists and sets SPAWN_EXISTING_HEAD_ID plus
# SPAWN_EXISTING_LIVENESS (live, stopped, or unavailable); 1 when none exists.
spawn_existing_head_status() {
    SPAWN_EXISTING_HEAD_ID=""
    SPAWN_EXISTING_LIVENESS=""
    _sehs_project="$(hydra_get_project_id 2>/dev/null)" || return 1
    SPAWN_EXISTING_HEAD_ID="$(state_v2_find_head_by_branch "$_sehs_project" "$1" 2>/dev/null)" || return 1
    SPAWN_EXISTING_LIVENESS="$(lifecycle_liveness "$1" 2>/dev/null || true)"
    case "$SPAWN_EXISTING_LIVENESS" in
        live|unavailable) ;;
        *) SPAWN_EXISTING_LIVENESS=stopped ;;
    esac
    return 0
}

# Explain why a branch cannot be spawned again and what to do instead.
# Usage: spawn_report_existing_head <branch>
spawn_report_existing_head() {
    _sreh_branch="$1"
    case "$SPAWN_EXISTING_LIVENESS" in
        live)
            _sreh_session="$(get_session_for_branch "$_sreh_branch" 2>/dev/null || true)"
            _sreh_where=""
            [ -z "$_sreh_session" ] || _sreh_where=" in session '$_sreh_session'"
            echo "Error: $_sreh_branch is already running$_sreh_where. Follow it with 'hydra tui', attach with 'hydra switch $_sreh_branch', or pick a new branch name." >&2
            ;;
        unavailable)
            echo "Error: $_sreh_branch already exists as a headless head without a live owner. Run work with 'hydra exec --branch $_sreh_branch -- <command>', start it again with 'hydra resume $_sreh_branch', or pick a new branch name." >&2
            ;;
        *)
            echo "Error: $_sreh_branch was removed earlier. Start it again with 'hydra resume $_sreh_branch', or pick a new branch name." >&2
            echo "Next: hydra spawn $_sreh_branch --resume does the same from this command" >&2
            ;;
    esac
}

# Resume a stopped head from `spawn --resume` instead of refusing it. The
# durable resume path owns admission and instance creation, so this never
# creates a second execution owner: a live head is rejected before we get here
# and again inside cmd_resume.
# Usage: spawn_resume_existing <branch> <dry_run>
spawn_resume_existing() {
    _sre_branch="$1"
    _sre_dry="$2"
    _sre_worktree="$(spawn_worktree_for_branch "$_sre_branch" 2>/dev/null || true)"
    _sre_mode="$(get_terminal_mode_for_branch "$_sre_branch" 2>/dev/null || echo interactive)"
    if [ -n "$_sre_dry" ]; then
        echo "Hydra spawn plan (no changes will be made)"
        echo "  branch: $_sre_branch"
        echo "  head_id: $SPAWN_EXISTING_HEAD_ID"
        echo "  action: resume existing head (same as 'hydra resume $_sre_branch')"
        echo "  worktree: ${_sre_worktree:--}"
        echo "  terminal_mode: $_sre_mode"
        echo "  state: create a new instance from durable resume metadata, then emit lifecycle.resumed"
        return 0
    fi
    _load_lib cmd_evidence
    cmd_resume "$_sre_branch" || return 1
    _sre_session="$(get_session_for_branch "$_sre_branch" 2>/dev/null || true)"
    _sre_profile="$(spawn_existing_profile "$_sre_branch")"
    spawn_finish_launch "$_sre_branch" "$_sre_session" "$_sre_profile" "$_sre_mode" resumed
}

# Usage: spawn_existing_profile <branch>
spawn_existing_profile() {
    _sep_project="$(hydra_get_project_id 2>/dev/null)" || return 0
    _sep_head="$(state_v2_find_head_by_branch "$_sep_project" "$1" 2>/dev/null)" || return 0
    _sep_dir="$(state_v2_head_dir "$_sep_project" "$_sep_head")" || return 0
    sed -n '1p' "$_sep_dir/profile" 2>/dev/null || true
}
