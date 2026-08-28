#!/bin/sh
# Maintenance and consistency check helpers for Hydra
# POSIX-compliant shell script
#
# Shared logic for doctor and cleanup commands.
# Dependencies: state.sh, tmux.sh, paths.sh, locks.sh

# Count mappings whose tmux session no longer exists
# Usage: count_dead_sessions
# Returns: Count on stdout
count_dead_sessions() {
    _dead=0
    if [ -z "${HYDRA_MAP:-}" ] || [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        printf '%s' "0"
        return 0
    fi
    while IFS=' ' read -r _branch _session _rest; do
        if [ -n "$_session" ] && ! tmux_session_exists "$_session"; then
            _dead=$((_dead + 1))
        fi
    done < "$HYDRA_MAP"
    printf '%s' "$_dead"
}

# Check if a branch has a mapping in the state file
# Usage: branch_has_mapping <branch>
# Returns: 0 if mapped, 1 otherwise
branch_has_mapping() {
    _branch="$1"
    if [ -z "$_branch" ] || [ ! -f "${HYDRA_MAP:-}" ] || [ ! -s "$HYDRA_MAP" ]; then
        return 1
    fi
    while IFS=' ' read -r _mapped_branch _rest; do
        if [ "$_mapped_branch" = "$_branch" ]; then
            return 0
        fi
    done < "$HYDRA_MAP"
    return 1
}

# Count hydra worktrees without a state mapping
# Usage: count_orphan_worktrees [repo_root]
# Returns: Count on stdout
count_orphan_worktrees() {
    _repo_root="${1:-}"
    if [ -z "$_repo_root" ]; then
        _repo_root="$(get_repo_root 2>/dev/null || true)"
    fi
    if [ -z "$_repo_root" ]; then
        printf '%s' "0"
        return 0
    fi

    _orphan=0
    while IFS='	' read -r _branch _path; do
        [ -z "$_branch" ] && continue
        if ! branch_has_mapping "$_branch"; then
            _orphan=$((_orphan + 1))
        fi
    done <<EOF
$(list_hydra_worktrees "$_repo_root")
EOF
    printf '%s' "$_orphan"
}

# List paths of orphaned hydra worktrees (no state mapping)
# Usage: list_orphan_worktree_paths [repo_root]
# Returns: One path per line on stdout
list_orphan_worktree_paths() {
    _repo_root="${1:-}"
    if [ -z "$_repo_root" ]; then
        _repo_root="$(get_repo_root 2>/dev/null || true)"
    fi
    if [ -z "$_repo_root" ]; then
        return 0
    fi

    while IFS='	' read -r _branch _path; do
        [ -z "$_branch" ] && continue
        if ! branch_has_mapping "$_branch"; then
            printf '%s\n' "$_path"
        fi
    done <<EOF
$(list_hydra_worktrees "$_repo_root")
EOF
}

# Count stale lock directories (mkdir *.lock dirs older than 1 minute)
# Usage: count_stale_locks
# Returns: Count on stdout
count_stale_locks() {
    if [ -z "${HYDRA_HOME:-}" ] || [ ! -d "$HYDRA_HOME/locks" ]; then
        printf '%s' "0"
        return 0
    fi
    _stale="$(find "$HYDRA_HOME/locks" -name "*.lock" -type d -mmin +1 2>/dev/null | wc -l | tr -d ' ')"
    printf '%s' "${_stale:-0}"
}

# Remove stale lock directories (mkdir *.lock dirs older than 1 minute)
# Usage: clean_stale_locks
# Returns: Number removed on stdout
clean_stale_locks() {
    if [ -z "${HYDRA_HOME:-}" ] || [ ! -d "$HYDRA_HOME/locks" ]; then
        printf '%s' "0"
        return 0
    fi
    _cleaned=0
    # Collect paths first so rmdir does not race with find
    _stale_list="$(find "$HYDRA_HOME/locks" -name "*.lock" -type d -mmin +1 2>/dev/null || true)"
    if [ -n "$_stale_list" ]; then
        while IFS= read -r _lock_dir; do
            [ -n "$_lock_dir" ] || continue
            if rmdir "$_lock_dir" 2>/dev/null; then
                _cleaned=$((_cleaned + 1))
            fi
        done <<EOF
$_stale_list
EOF
    fi
    printf '%s' "$_cleaned"
}

# Remove dead session mappings from state file
# Usage: clean_dead_mappings
# Returns: Number removed on stdout
clean_dead_mappings() {
    _dead_cleaned=0
    if [ -z "${HYDRA_MAP:-}" ] || [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        printf '%s' "0"
        return 0
    fi

    if ! acquire_lock "state_map"; then
        echo "Error: Failed to acquire state lock" >&2
        printf '%s' "0"
        return 1
    fi

    _tmpfile="$(mktemp_adjacent "$HYDRA_MAP")" || {
        release_lock "state_map"
        printf '%s' "0"
        return 1
    }
    _dead_branches=""
    while IFS=' ' read -r _branch _session _ai _group _timestamp _deps _pr; do
        if [ -n "$_session" ] && tmux_session_exists "$_session"; then
            printf '%s %s %s %s %s %s %s\n' \
                "$_branch" "$_session" "${_ai:--}" "${_group:--}" "${_timestamp:--}" "${_deps:--}" "${_pr:--}" >> "$_tmpfile"
        else
            _dead_cleaned=$((_dead_cleaned + 1))
            if [ -n "$_branch" ]; then
                _dead_branches="${_dead_branches}${_branch}
"
            fi
        fi
    done < "$HYDRA_MAP"
    if ! atomic_replace "$HYDRA_MAP" "$_tmpfile"; then
        rm -f "$_tmpfile"
        release_lock "state_map"
        printf '%s' "0"
        return 1
    fi
    _invalidate_state_cache
    release_lock "state_map"

    if [ -n "$_dead_branches" ] && command -v cleanup_messages_for_branch >/dev/null 2>&1; then
        while IFS= read -r _dead_b; do
            [ -n "$_dead_b" ] || continue
            cleanup_messages_for_branch "$_dead_b"
        done <<EOF
$_dead_branches
EOF
    fi
    printf '%s' "$_dead_cleaned"
}
