#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_spawn() {
    # Parse arguments
    branch=""
    layout="default"
    count=1
    ai_tool=""
    agents_spec=""
    issue_num=""
    group=""
    after_deps=""
    pr_num=""
    pr_new=""
    template_name=""

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
            --ai)
                shift
                ai_tool="$1"
                shift
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
    
    # Validate count
    if ! echo "$count" | grep -q '^[0-9]\+$' || [ "$count" -lt 1 ] || [ "$count" -gt 10 ]; then
        echo "Error: Count must be a number between 1 and 10" >&2
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
    if [ -n "$agents_spec" ] && [ -n "$ai_tool" ]; then
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

    if session="$(spawn_single "$branch" "$layout" "$ai_tool" "$group" "$after_deps" "$spawn_pr_num" "$template_name")"; then
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
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra queue [clear|remove <branch>|process] [--json]" >&2
                exit 1
                ;;
            *)
                echo "Error: Unknown action '$1'" >&2
                echo "Usage: hydra queue [clear|remove <branch>|process] [--json]" >&2
                exit 1
                ;;
        esac
    done

    case "$action" in
        clear)
            count="$(clear_queue)"
            echo "Cleared $count queued spawn(s)"
            ;;
        remove)
            if [ -z "$target_branch" ]; then
                echo "Error: Branch name required" >&2
                echo "Usage: hydra queue remove <branch>" >&2
                exit 1
            fi
            if dequeue_spawn "$target_branch"; then
                echo "Removed '$target_branch' from queue"
            else
                echo "Branch '$target_branch' not found in queue"
                exit 1
            fi
            ;;
        process)
            spawned="$(process_spawn_queue)"
            echo "Processed queue: $spawned session(s) spawned"
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
    echo "Regenerating tmux sessions for existing worktrees..."
    
    # Best-effort cleanup of stale session-name locks
    cleanup_stale_locks 2>/dev/null || true

    # Get repository root
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "Error: Not in a git repository" >&2
        echo "Next: cd into an existing git repo, or create a throwaway repo (see README Quick Start)" >&2
        return 1
    fi
    
    repo_root="$(get_repo_root)" || return 1
    
    # Find all hydra worktrees via git worktree list
    regenerated=0
    skipped=0
    
    while IFS='	' read -r branch dir; do
        [ -z "$branch" ] || [ -z "$dir" ] && continue
        
        # Check if session already exists
        existing_session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
        if [ -n "$existing_session" ] && tmux_session_exists "$existing_session"; then
            echo "Session already exists for '$branch', skipping..."
            skipped=$((skipped + 1))
            continue
        fi
        
        # Generate session name
        session="$(generate_session_name "$branch")"
        
        # Create session
        echo "Creating session '$session' for branch '$branch'..."
        if create_session "$session" "$dir"; then
            # Preserve any stored AI tool for this branch
            stored_ai="$(get_ai_for_branch "$branch" 2>/dev/null || true)"
            stored_group="$(get_group_for_branch "$branch" 2>/dev/null || true)"
            stored_ts="$(get_timestamp_for_branch "$branch" 2>/dev/null || true)"
            stored_deps="$(get_deps_for_branch "$branch" 2>/dev/null || true)"
            stored_pr="$(get_pr_for_branch "$branch" 2>/dev/null || true)"
            add_mapping "$branch" "$session" "$stored_ai" "$stored_group" \
                "${stored_ts:-}" "$stored_deps" "$stored_pr"
            regenerated=$((regenerated + 1))
            # Release any reserved session name lock
            release_session_lock "$session" 2>/dev/null || true
            # Apply YAML config if available; else apply custom/built-in default
            repo_root_for_dir="$(git -C "$dir" rev-parse --show-toplevel 2>/dev/null || dirname "$dir")"
            if [ -z "${HYDRA_DISABLE_YAML:-}" ] && cfgpath="$(locate_yaml_config "$dir" "$repo_root_for_dir" 2>/dev/null || true)" && [ -n "$cfgpath" ]; then
                apply_yaml_config "$cfgpath" "$session" "$dir" "$repo_root_for_dir"
            else
                apply_custom_layout_or_default "default" "$session" "$dir" "$repo_root_for_dir"
                # Optionally run startup commands on regenerate only if explicitly enabled
                if [ -n "${HYDRA_REGENERATE_RUN_STARTUP:-}" ]; then
                    run_startup_commands "$session" "$dir" "$repo_root_for_dir"
                fi
            fi
            # Optionally auto-launch stored AI tool in the regenerated session
            if [ -n "$stored_ai" ]; then
                if validate_ai_command "$stored_ai"; then
                    echo "Starting $stored_ai in session '$session'..." >&2
                    send_keys_to_session "$session" "$stored_ai"
                else
                    echo "Warning: Stored AI tool '$stored_ai' is invalid; skipping launch" >&2
                fi
            fi
        else
            echo "Failed to create session for '$branch'" >&2
            # Release any reserved session name lock
            release_session_lock "$session" 2>/dev/null || true
        fi
    done <<EOF
$(list_hydra_worktrees "$repo_root")
EOF
    
    echo ""
    echo "Regeneration complete:"
    echo "  Created: $regenerated"
    echo "  Skipped: $skipped"
}

