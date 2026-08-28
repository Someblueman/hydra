#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_spawn() {
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
    dry_run=""
    task_text=""
    task_source=""
    prompt_file=""
    use_issue_body=""
    completion_policy=declared-done

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
                echo "Usage: hydra spawn <branch> [-l|--layout <layout>] [-n|--count <number>] [--ai <tool>] [--agents <spec>] [-g|--group <name>] [--after <deps>] [-t|--template <name>]" >&2
                echo "       hydra spawn --issue <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                echo "       hydra spawn --pr <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                exit 1
                ;;
            *)
                if [ -z "$branch" ]; then
                    branch="$1"
                else
                    echo "Error: Too many arguments" >&2
                    echo "Usage: hydra spawn <branch> [-l|--layout <layout>] [-n|--count <number>] [--ai <tool>] [--agents <spec>] [-g|--group <name>] [--after <deps>] [-t|--template <name>]" >&2
                    echo "       hydra spawn --issue <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                    echo "       hydra spawn --pr <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
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
        echo "Usage: hydra spawn <branch> [-l|--layout <layout>] [-n|--count <number>] [--ai <tool>] [--agents <spec>] [-g|--group <name>]" >&2
        echo "       hydra spawn --issue <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
        echo "       hydra spawn --pr <number> [-l|--layout <layout>] [-g|--group <name>]" >&2
        exit 1
    fi

    if [ -n "$no_agent" ] && { [ -n "$explicit_profile" ] || [ -n "$agents_spec" ]; }; then
        echo "Error: --no-agent cannot be combined with --profile/--ai" >&2
        exit 1
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
        [ -f "$prompt_file" ] && [ -r "$prompt_file" ] || {
            echo "Error: prompt file is not readable: $prompt_file" >&2
            exit 1
        }
        task_bytes="$(LC_ALL=C wc -c < "$prompt_file" | tr -d ' ')"
        [ "$task_bytes" -le 65536 ] || { echo "Error: task input exceeds 65536 bytes" >&2; exit 1; }
        task_text="$(sed -n '1,$p' "$prompt_file")"
    fi
    if [ -n "$use_issue_body" ]; then
        [ -n "$issue_num" ] || { echo "Error: --issue-body requires --issue <number>" >&2; exit 1; }
        task_text="$(get_issue_body "$issue_num")" || exit 1
    fi
    task_bytes="$(printf '%s' "$task_text" | LC_ALL=C wc -c | tr -d ' ')"
    [ "$task_bytes" -le 65536 ] || { echo "Error: task input exceeds 65536 bytes" >&2; exit 1; }

    # Validate count
    if ! echo "$count" | grep -q '^[0-9]\+$' || [ "$count" -lt 1 ] || [ "$count" -gt 10 ]; then
        echo "Error: Count must be a number between 1 and 10" >&2
        exit 1
    fi

    if { [ -n "$task_source" ] || [ -n "$dry_run" ]; } && \
       { [ "$count" -gt 1 ] || [ -n "$agents_spec" ]; }; then
        echo "Error: task injection and dry-run currently require a single head" >&2
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

    if [ -n "$dry_run" ]; then
        spawn_dry_run "$branch" "$layout" "$ai_tool" "$group" "$after_deps" "$pr_num" "$template_name" "$task_text" "$completion_policy"
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

    if session="$(spawn_single "$branch" "$layout" "$ai_tool" "$group" "$after_deps" "$spawn_pr_num" "$template_name" "$task_text" "$completion_policy")"; then
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

        # Optionally skip switching (useful for demos/automation)
        if [ -n "${HYDRA_NO_SWITCH:-}" ]; then
            echo "Session '$session' created (HYDRA_NO_SWITCH set; not attaching)"
        else
            # Switch to the new session (only in terminal)
            if [ -t 0 ] && [ -t 1 ]; then
                echo "Switching to session '$session'..."
                switch_to_session "$session"
            else
                echo "Session '$session' created successfully (not switching - not in terminal)"
            fi
        fi
        return 0
    else
        return 1
    fi
}

cmd_queue() {
    # Parse arguments
    action=""
    json_output=""
    target_branch=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -j|--json)
                json_output="1"
                shift
                ;;
            clear)
                action="clear"
                shift
                ;;
            remove)
                action="remove"
                shift
                if [ $# -gt 0 ]; then
                    target_branch="$1"
                    shift
                fi
                ;;
            process)
                action="process"
                shift
                ;;
            -*)
                cli_error queue invalid_input "Unknown option '$1'" "run hydra queue --json"
                return 1
                ;;
            *)
                cli_error queue invalid_input "Unknown action '$1'" "run hydra queue --json"
                return 1
                ;;
        esac
    done

    case "$action" in
        clear)
            count="$(clear_queue)"
            if [ -n "$json_output" ]; then
                json_success queue "{\"action\":\"clear\",\"cleared\":$count}"
            else
                echo "Cleared $count queued spawn(s)"
            fi
            ;;
        remove)
            if [ -z "$target_branch" ]; then
                cli_error queue invalid_input "Branch name required" "run hydra queue remove <branch> --json"
                return 1
            fi
            if dequeue_spawn "$target_branch"; then
                if [ -n "$json_output" ]; then
                    json_success queue "{\"action\":\"remove\",\"branch\":\"$(json_escape "$target_branch")\"}"
                else
                    echo "Removed '$target_branch' from queue"
                fi
            else
                if [ -n "$json_output" ]; then
                    json_error queue not_found "Branch '$target_branch' not found in queue" "inspect with hydra queue --json"
                else
                    echo "Branch '$target_branch' not found in queue"
                fi
                return 1
            fi
            ;;
        process)
            spawned="$(process_spawn_queue)"
            if [ -n "$json_output" ]; then
                json_success queue "{\"action\":\"process\",\"spawned\":$spawned}"
            else
                echo "Processed queue: $spawned session(s) spawned"
            fi
            ;;
        *)
            # Default: list queue
            if [ -n "$json_output" ]; then
                list_queue --json
            else
                echo "Spawn Queue:"
                echo "============"
                echo ""
                list_queue
            fi
            ;;
    esac
}

cmd_regenerate() {
    echo "Regenerating dead heads as new lifecycle instances..."
    cleanup_stale_locks 2>/dev/null || true
    if { [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; } && \
       ! hydra_get_project_id >/dev/null 2>&1; then
        echo "No Hydra heads to regenerate"
        return 0
    fi
    hydra_get_project_id >/dev/null 2>&1 || {
        echo "Error: project is not initialized; run hydra init first" >&2
        return 1
    }
    regenerated=0
    skipped=0
    failed=0
    while IFS=' ' read -r branch session _ai _group _timestamp _deps _pr; do
        [ -n "$branch" ] || continue
        if [ -n "$session" ] && tmux_session_exists "$session"; then
            echo "Session already exists for '$branch'; skipping"
            skipped=$((skipped + 1))
            continue
        fi
        if cmd_resume "$branch"; then
            regenerated=$((regenerated + 1))
        else
            failed=$((failed + 1))
        fi
    done < "$HYDRA_MAP"
    echo ""
    echo "Regeneration complete:"
    echo "  Created: $regenerated"
    echo "  Skipped: $skipped"
    echo "  Failed: $failed"
    [ "$failed" -eq 0 ]
}
