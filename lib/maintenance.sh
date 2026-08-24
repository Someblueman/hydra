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
    grep -q "^${_branch} " "$HYDRA_MAP" 2>/dev/null
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

# Count stale lock directories (older than 60 seconds)
# Usage: count_stale_locks
# Returns: Count on stdout
count_stale_locks() {
    _stale=0
    if [ ! -d "${HYDRA_HOME:-}/locks" ]; then
        printf '%s' "0"
        return 0
    fi
    for _lock_dir in "$HYDRA_HOME/locks"/*; do
        [ -d "$_lock_dir" ] || continue
        _lock_age=0
        if [ -f "$_lock_dir/pid" ]; then
            if command -v stat >/dev/null 2>&1; then
                if stat --version 2>&1 | grep -q GNU; then
                    _lock_mtime="$(stat -c %Y "$_lock_dir/pid" 2>/dev/null || echo 0)"
                else
                    _lock_mtime="$(stat -f %m "$_lock_dir/pid" 2>/dev/null || echo 0)"
                fi
                _now="$(date +%s)"
                _lock_age=$((_now - _lock_mtime))
            fi
        fi
        if [ "$_lock_age" -gt 60 ]; then
            _stale=$((_stale + 1))
        fi
    done
    printf '%s' "$_stale"
}

# Remove stale lock directories
# Usage: clean_stale_locks
# Returns: Number removed on stdout
clean_stale_locks() {
    _cleaned=0
    if [ ! -d "${HYDRA_HOME:-}/locks" ]; then
        printf '%s' "0"
        return 0
    fi
    for _lock_dir in "$HYDRA_HOME/locks"/*; do
        [ -d "$_lock_dir" ] || continue
        _lock_age=0
        if [ -f "$_lock_dir/pid" ]; then
            if command -v stat >/dev/null 2>&1; then
                if stat --version 2>&1 | grep -q GNU; then
                    _lock_mtime="$(stat -c %Y "$_lock_dir/pid" 2>/dev/null || echo 0)"
                else
                    _lock_mtime="$(stat -f %m "$_lock_dir/pid" 2>/dev/null || echo 0)"
                fi
                _now="$(date +%s)"
                _lock_age=$((_now - _lock_mtime))
            fi
        fi
        if [ "$_lock_age" -gt 60 ]; then
            rm -rf "$_lock_dir"
            _cleaned=$((_cleaned + 1))
        fi
    done
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
    _tmpfile="$(mktemp)"
    while IFS=' ' read -r _branch _session _ai _group _timestamp _deps _pr; do
        if [ -n "$_session" ] && tmux_session_exists "$_session"; then
            printf '%s %s %s %s %s %s %s\n' \
                "$_branch" "$_session" "${_ai:--}" "${_group:--}" "${_timestamp:--}" "${_deps:--}" "${_pr:--}" >> "$_tmpfile"
        else
            _dead_cleaned=$((_dead_cleaned + 1))
        fi
    done < "$HYDRA_MAP"
    mv "$_tmpfile" "$HYDRA_MAP"
    _invalidate_state_cache
    printf '%s' "$_dead_cleaned"
}
