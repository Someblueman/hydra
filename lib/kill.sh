#!/bin/sh
# Kill session functions for Hydra
# POSIX-compliant shell script
#
# Provides session kill capabilities for single and bulk operations.
# Dependencies: paths.sh, git.sh, tmux.sh, state.sh

# Kill a single hydra head (session + worktree + mapping)
# Usage: kill_single_head <branch> <session>
# Returns: 0 on success, 1 on failure
kill_single_head() {
    branch="$1"
    session="$2"

    if [ -z "$branch" ] || [ -z "$session" ]; then
        echo "Error: Branch and session are required" >&2
        return 1
    fi

    # Kill tmux session if it exists
    if tmux_session_exists "$session"; then
        echo "  Killing tmux session '$session'..."
        if ! tmux kill-session -t "$session" 2>/dev/null; then
            echo "  Failed to kill tmux session '$session'" >&2
            return 1
        fi
    fi

    # Remove mapping
    remove_mapping "$branch"

    # Delete worktree using consolidated path function
    worktree_path="$(get_worktree_path_for_branch "$branch" 2>/dev/null || true)"
    if [ -n "$worktree_path" ] && [ -d "$worktree_path" ]; then
        normalized_path="$(normalize_path "$worktree_path")"
        echo "  Removing worktree at '$normalized_path'..."
        if ! delete_worktree "$normalized_path"; then
            echo "  Failed to remove worktree for '$branch'" >&2
            return 1
        fi
    fi

    return 0
}

# Kill all active hydra sessions
# Usage: kill_all_sessions [force]
# force: "true" to skip confirmation
# Returns: 0 on success, 1 on failure
kill_all_sessions() {
    force="${1:-false}"

    # Check if we have any mappings
    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        echo "No active Hydra heads to kill"
        return 0
    fi

    # Get all mappings
    mappings="$(list_mappings)"
    count="$(echo "$mappings" | wc -l | tr -d ' ')"

    if [ "$count" -eq 0 ]; then
        echo "No active Hydra heads to kill"
        return 0
    fi

    # Display what will be killed
    echo "The following hydra heads will be killed:"
    while IFS=' ' read -r branch session; do
        if [ -n "$branch" ] && [ -n "$session" ]; then
            echo "  $branch -> $session"
        fi
    done <<EOF
$mappings
EOF

    # Handle confirmation
    if [ "$force" != "true" ]; then
        # Check if we're in interactive mode
        if [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
            # Interactive mode - ask for confirmation
            printf "\nKill all %d hydra heads? [y/N] " "$count"
            read -r response
            case "$response" in
                [yY][eE][sS]|[yY])
                    ;;
                *)
                    echo "Aborted"
                    return 0
                    ;;
            esac
        else
            # Non-interactive mode without force - fail safe
            echo "Error: Cannot kill all sessions in non-interactive mode without --force" >&2
            return 1
        fi
    else
        echo ""
        echo "Killing all $count hydra heads (--force specified)..."
    fi

    # Kill each session
    succeeded=0
    failed=0

    # Process each mapping
    while IFS=' ' read -r branch session; do
        if [ -z "$branch" ] || [ -z "$session" ]; then
            continue
        fi

        echo ""
        echo "Killing hydra head '$branch' (session: $session)..."

        # Kill tmux session
        if tmux_session_exists "$session"; then
            echo "  Killing tmux session '$session'..."
            if tmux kill-session -t "$session" 2>/dev/null; then
                # Remove mapping
                remove_mapping "$branch"

                # Delete worktree using consolidated path function
                worktree_path="$(get_worktree_path_for_branch "$branch" 2>/dev/null || true)"
                if [ -n "$worktree_path" ] && [ -d "$worktree_path" ]; then
                    normalized_path="$(normalize_path "$worktree_path")"
                    echo "  Removing worktree at '$normalized_path'..."
                    if delete_worktree "$normalized_path"; then
                        succeeded=$((succeeded + 1))
                        echo "  Successfully killed hydra head '$branch'"
                    else
                        failed=$((failed + 1))
                        echo "  Failed to remove worktree for '$branch'" >&2
                    fi
                else
                    # Session killed but no worktree found
                    succeeded=$((succeeded + 1))
                    echo "  Successfully killed session '$session' (no worktree found)"
                fi
            else
                failed=$((failed + 1))
                echo "  Failed to kill tmux session '$session'" >&2
            fi
        else
            # Session doesn't exist - clean up mapping anyway
            echo "  Session '$session' not found, cleaning up mapping..."
            remove_mapping "$branch"

            # Try to remove worktree if it exists
            worktree_path="$(get_worktree_path_for_branch "$branch" 2>/dev/null || true)"
            if [ -n "$worktree_path" ] && [ -d "$worktree_path" ]; then
                normalized_path="$(normalize_path "$worktree_path")"
                echo "  Removing orphaned worktree at '$normalized_path'..."
                delete_worktree "$normalized_path"
            fi
            succeeded=$((succeeded + 1))
        fi
    done <<EOF
$mappings
EOF

    # Summary
    echo ""
    echo "Kill all complete:"
    echo "  Succeeded: $succeeded"
    if [ "$failed" -gt 0 ]; then
        echo "  Failed: $failed"
        return 1
    fi

    return 0
}
