#!/bin/sh
# Spawn session functions for Hydra
# POSIX-compliant shell script
#
# Provides session spawning capabilities for single and bulk operations.
# Dependencies: locks.sh, paths.sh, git.sh, tmux.sh, state.sh, hooks.sh, yaml.sh

# Helper function to spawn a single session
# Usage: spawn_single <branch> <layout> [ai_tool] [group]
# Returns: Session name on stdout, 1 on failure
spawn_single() {
    branch="$1"
    layout="${2:-default}"
    ai_tool="${3:-}"
    group="${4:-}"

    # Best-effort cleanup of stale session-name locks
    cleanup_stale_locks 2>/dev/null || true

    # Check tmux availability
    if ! check_tmux_version; then
        return 1
    fi

    # Check if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "Error: Not in a git repository" >&2
        return 1
    fi

    # Get worktree path using consolidated path function
    worktree_path="$(get_worktree_path_for_branch "$branch")" || return 1
    repo_root="$(get_repo_root)" || return 1

    # Check if branch already has a session
    existing_session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
    if [ -n "$existing_session" ] && tmux_session_exists "$existing_session"; then
        echo "Error: Branch '$branch' already has an active session '$existing_session'" >&2
        echo "Use 'hydra switch' to switch to it" >&2
        return 1
    fi

    # Create worktree
    echo "Creating worktree for branch '$branch'..." >&2
    if ! create_worktree "$branch" "$worktree_path"; then
        return 1
    fi

    # Generate session name
    session="$(generate_session_name "$branch")"

    # Run pre-spawn hook (best-effort)
    run_hook pre-spawn "$worktree_path" "$repo_root" "" "$branch"

    # Create tmux session
    echo "Creating tmux session '$session'..." >&2
    if ! create_session "$session" "$worktree_path"; then
        # Clean up worktree if session creation failed
        # Release any reserved session name lock
        release_session_lock "$session" 2>/dev/null || true
        delete_worktree "$worktree_path" 2>/dev/null || true
        return 1
    fi
    # Release the reserved session name lock now that session is created
    release_session_lock "$session" 2>/dev/null || true

    # Add mapping (persist selected AI tool and group if provided)
    if ! add_mapping "$branch" "$session" "${ai_tool:-}" "${group:-}"; then
        echo "Warning: Failed to save branch-session mapping" >&2
    fi

    # Apply YAML config if present; otherwise custom/built-in layout
    if [ -z "${HYDRA_DISABLE_YAML:-}" ] && cfgpath="$(locate_yaml_config "$worktree_path" "$repo_root" 2>/dev/null || true)" && [ -n "$cfgpath" ]; then
        apply_yaml_config "$cfgpath" "$session" "$worktree_path" "$repo_root"
    else
        apply_custom_layout_or_default "$layout" "$session" "$worktree_path" "$repo_root"
        # Send optional startup commands
        run_startup_commands "$session" "$worktree_path" "$repo_root"
    fi

    # Start AI tool unless explicitly skipped (e.g., demos/CI)
    if [ -z "${HYDRA_SKIP_AI:-}" ]; then
        if [ -z "$ai_tool" ]; then
            ai_tool="${HYDRA_AI_COMMAND:-claude}"
        fi
        if ! validate_ai_command "$ai_tool"; then
            return 1
        fi
        echo "Starting $ai_tool in session '$session'..." >&2
        send_keys_to_session "$session" "$ai_tool"
    fi

    # Run post-spawn hook (best-effort)
    run_hook post-spawn "$worktree_path" "$repo_root" "$session" "$branch"

    # Return session name for caller
    echo "$session"
    return 0
}

# Spawn multiple sessions with same AI tool
# Usage: spawn_bulk <base_branch> <count> <layout> [ai_tool] [group]
# Note: Calls cmd_kill for rollback, which must be defined in bin/hydra
spawn_bulk() {
    base_branch="$1"
    count="$2"
    layout="${3:-default}"
    ai_tool="${4:-}"
    group="${5:-}"

    # Confirm if spawning many sessions
    if [ "$count" -gt 3 ]; then
        printf "Are you sure you want to spawn %d sessions? [y/N] " "$count"
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                ;;
            *)
                echo "Aborted" >&2
                return 1
                ;;
        esac
    fi

    echo "Spawning $count sessions based on '$base_branch'..."

    succeeded=0
    failed=0
    created_branches=""

    i=1
    while [ "$i" -le "$count" ]; do
        branch_name="${base_branch}-${i}"
        echo ""
        echo "[$i/$count] Creating head '$branch_name'..."

        if session="$(spawn_single "$branch_name" "$layout" "$ai_tool" "$group")"; then
            succeeded=$((succeeded + 1))
            created_branches="$created_branches $branch_name"
            echo "[$i/$count] Successfully created session: $session"
        else
            failed=$((failed + 1))
            echo "[$i/$count] Failed to create head '$branch_name'" >&2

            # Ask whether to continue or rollback
            if [ "$i" -lt "$count" ]; then
                printf "Continue with remaining heads? [y/N] "
                read -r response
                case "$response" in
                    [yY][eE][sS]|[yY])
                        ;;
                    *)
                        echo "Rolling back created heads..."
                        for b in $created_branches; do
                            echo "Removing $b..."
                            cmd_kill "$b" >/dev/null 2>&1 || true
                        done
                        return 1
                        ;;
                esac
            fi
        fi

        i=$((i + 1))
    done

    echo ""
    echo "Bulk spawn complete:"
    echo "  Succeeded: $succeeded"
    echo "  Failed: $failed"

    # Switch to first created session if in terminal
    if [ -t 0 ] && [ -t 1 ] && [ "$succeeded" -gt 0 ]; then
        first_branch="$(echo "$created_branches" | cut -d' ' -f2)"
        first_session="$(get_session_for_branch "$first_branch" 2>/dev/null || true)"
        if [ -n "$first_session" ]; then
            echo ""
            echo "Switching to first session '$first_session'..."
            switch_to_session "$first_session"
        fi
    fi

    return 0
}

# Spawn sessions with mixed AI agents
# Usage: spawn_bulk_mixed <base_branch> <agents_spec> <layout> [group]
# agents_spec format: "claude:2,aider:1,codex:1"
# Note: Calls cmd_kill for rollback, which must be defined in bin/hydra
spawn_bulk_mixed() {
    base_branch="$1"
    agents_spec="$2"
    layout="${3:-default}"
    group="${4:-}"

    echo "Parsing agents specification: $agents_spec"

    # Parse the agents spec and create sessions
    total_count=0
    session_num=1
    succeeded=0
    failed=0
    created_branches=""

    # Process each agent:count pair
    while [ -n "$agents_spec" ]; do
        # Extract first agent:count pair
        pair="${agents_spec%%,*}"

        # Remove processed pair from spec
        if [ "$pair" = "$agents_spec" ]; then
            agents_spec=""
        else
            agents_spec="${agents_spec#*,}"
        fi

        # Check if pair contains a colon
        case "$pair" in
            *:*)
                # Parse agent and count
                agent="${pair%%:*}"
                agent_count="${pair#*:}"
                ;;
            *)
                echo "Error: Invalid agent specification: $pair" >&2
                return 1
                ;;
        esac

        # Validate
        if [ -z "$agent" ] || [ -z "$agent_count" ]; then
            echo "Error: Invalid agent specification: $pair" >&2
            return 1
        fi

        if ! echo "$agent_count" | grep -q '^[0-9]\+$' || [ "$agent_count" -lt 1 ]; then
            echo "Error: Invalid count for $agent: $agent_count" >&2
            return 1
        fi

        if ! validate_ai_command "$agent"; then
            return 1
        fi

        total_count=$((total_count + agent_count))
    done

    # Confirm if spawning many sessions
    if [ "$total_count" -gt 3 ]; then
        printf "Are you sure you want to spawn %d sessions? [y/N] " "$total_count"
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                ;;
            *)
                echo "Aborted" >&2
                return 1
                ;;
        esac
    fi

    # Reset agents_spec for actual processing
    agents_spec="$2"

    echo "Spawning $total_count sessions with mixed agents..."

    # Process each agent:count pair again for actual spawning
    while [ -n "$agents_spec" ]; do
        # Extract first agent:count pair
        pair="${agents_spec%%,*}"

        # Remove processed pair from spec
        if [ "$pair" = "$agents_spec" ]; then
            agents_spec=""
        else
            agents_spec="${agents_spec#*,}"
        fi

        # Parse agent and count (already validated in first loop)
        agent="${pair%%:*}"
        agent_count="${pair#*:}"

        echo ""
        echo "Creating $agent_count session(s) with $agent..."

        i=1
        while [ "$i" -le "$agent_count" ]; do
            branch_name="${base_branch}-${session_num}"
            echo ""
            echo "[$session_num/$total_count] Creating head '$branch_name' with $agent..."

            if session="$(spawn_single "$branch_name" "$layout" "$agent" "$group")"; then
                succeeded=$((succeeded + 1))
                created_branches="$created_branches $branch_name"
                echo "[$session_num/$total_count] Successfully created session: $session"
            else
                failed=$((failed + 1))
                echo "[$session_num/$total_count] Failed to create head '$branch_name'" >&2

                # Ask whether to continue or rollback
                if [ "$session_num" -lt "$total_count" ]; then
                    printf "Continue with remaining heads? [y/N] "
                    read -r response
                    case "$response" in
                        [yY][eE][sS]|[yY])
                            ;;
                        *)
                            echo "Rolling back created heads..."
                            for b in $created_branches; do
                                echo "Removing $b..."
                                cmd_kill "$b" >/dev/null 2>&1 || true
                            done
                            return 1
                            ;;
                    esac
                fi
            fi

            i=$((i + 1))
            session_num=$((session_num + 1))
        done
    done

    echo ""
    echo "Bulk spawn complete:"
    echo "  Succeeded: $succeeded"
    echo "  Failed: $failed"

    # Switch to first created session if in terminal
    if [ -t 0 ] && [ -t 1 ] && [ "$succeeded" -gt 0 ]; then
        first_branch="$(echo "$created_branches" | cut -d' ' -f2)"
        first_session="$(get_session_for_branch "$first_branch" 2>/dev/null || true)"
        if [ -n "$first_session" ]; then
            echo ""
            echo "Switching to first session '$first_session'..."
            switch_to_session "$first_session"
        fi
    fi

    return 0
}
