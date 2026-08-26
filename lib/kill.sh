#!/bin/sh
# Kill session functions for Hydra
# POSIX-compliant shell script
#
# Provides session kill capabilities for single and bulk operations.
# Dependencies: paths.sh, git.sh, tmux.sh, state.sh

# Load message cleanup if available
if ! command -v cleanup_messages_for_branch >/dev/null 2>&1; then
    _kill_lib_dir="${HYDRA_LIB_DIR:-}"
    if [ -z "$_kill_lib_dir" ]; then
        _kill_lib_dir="$(cd "$(dirname "$0")/../lib" 2>/dev/null && pwd)" || true
    fi
    if [ -n "$_kill_lib_dir" ] && [ -f "$_kill_lib_dir/messages.sh" ]; then
        # shellcheck disable=SC1090,SC1091
        . "$_kill_lib_dir/messages.sh"
    fi
fi

# Get worktree path for a branch with fallback
# Usage: get_worktree_path_with_fallback <branch>
# Returns: Worktree path on stdout, 1 if not found
# Note: Tries repo-relative path first, then git worktree list
get_worktree_path_with_fallback() {
    _branch="$1"
    if [ -z "$_branch" ]; then
        return 1
    fi

    # Try primary method: calculate from repo root
    if _path="$(get_worktree_path_for_branch "$_branch" 2>/dev/null)"; then
        echo "$_path"
        return 0
    fi

    # Fallback: use git worktree list to find existing worktree
    if _path="$(find_worktree_path "$_branch" 2>/dev/null)"; then
        echo "$_path"
        return 0
    fi

    return 1
}

# Preflight a head before destroying tmux or state.
# Usage: _kill_preflight <branch>
# Returns: 0 if teardown may proceed, 1 if the worktree is not removable
_kill_preflight() {
    _pf_branch="$1"
    _pf_path="$(get_worktree_path_with_fallback "$_pf_branch" || true)"
    if [ -n "$_pf_path" ] && [ -d "$_pf_path" ]; then
        _pf_norm="$(normalize_path "$_pf_path")"
        if ! check_worktree_removable "$_pf_norm"; then
            echo "Error: Refusing to kill '$_pf_branch'; session and mapping left intact" >&2
            return 1
        fi
    fi
    return 0
}

# Tear down tmux, mapping, worktree, and messages after a successful preflight.
# Usage: _kill_teardown <branch> <session>
# Returns: 0 on success, 1 if worktree deletion failed
_kill_teardown() {
    _td_branch="$1"
    _td_session="$2"

    # Hold the map lock before any irreversible tmux teardown so lock
    # contention cannot destroy the session while leaving the mapping.
    if ! _lock_state_map; then
        echo "  Failed to acquire state lock; session left intact" >&2
        return 1
    fi

    if tmux_session_exists "$_td_session"; then
        echo "  Killing tmux session '$_td_session'..."
        if ! tmux kill-session -t "$_td_session" 2>/dev/null; then
            echo "  Failed to kill tmux session '$_td_session'" >&2
            release_lock "state_map"
            return 1
        fi
        tmux_clear_snapshot
    else
        echo "  Session '$_td_session' not found, cleaning up mapping..."
    fi

    if ! _remove_mapping_locked "$_td_branch"; then
        echo "  Failed to update mapping for '$_td_branch'; worktree left in place" >&2
        release_lock "state_map"
        return 1
    fi
    release_lock "state_map"

    _td_path="$(get_worktree_path_with_fallback "$_td_branch" || true)"
    if [ -n "$_td_path" ] && [ -d "$_td_path" ]; then
        _td_norm="$(normalize_path "$_td_path")"
        echo "  Removing worktree at '$_td_norm'..."
        if ! delete_worktree "$_td_norm" "skip_preflight"; then
            echo "  Failed to remove worktree for '$_td_branch'" >&2
            return 1
        fi
    fi

    if command -v cleanup_messages_for_branch >/dev/null 2>&1; then
        cleanup_messages_for_branch "$_td_branch"
    fi

    return 0
}

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

    if ! _kill_preflight "$branch"; then
        return 1
    fi

    if ! _kill_teardown "$branch" "$session"; then
        return 1
    fi

    # Process pending spawn queue after successful kill (best-effort)
    # This allows queued spawns to proceed now that capacity is available
    if command -v process_spawn_queue >/dev/null 2>&1; then
        process_spawn_queue >/dev/null 2>&1 || true
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
    while IFS=' ' read -r branch session _ai _group _ts; do
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
    while IFS=' ' read -r branch session _ai _group _ts; do
        if [ -z "$branch" ] || [ -z "$session" ]; then
            continue
        fi

        echo ""
        echo "Killing hydra head '$branch' (session: $session)..."

        if ! _kill_preflight "$branch"; then
            failed=$((failed + 1))
            continue
        fi

        if _kill_teardown "$branch" "$session"; then
            succeeded=$((succeeded + 1))
            echo "  Successfully killed hydra head '$branch'"
        else
            failed=$((failed + 1))
        fi
    done <<EOF
$mappings
EOF

    if command -v process_spawn_queue >/dev/null 2>&1; then
        process_spawn_queue >/dev/null 2>&1 || true
    fi

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
