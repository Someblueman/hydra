#!/bin/sh
# Maintenance and consistency check helpers for Hydra
# POSIX-compliant shell script
#
# Shared logic for doctor and cleanup commands.
# Dependencies: state.sh, tmux.sh, paths.sh, locks.sh, worktree_ops.sh (orphans)

# Count active head records whose tmux session no longer exists
# Usage: count_dead_sessions
# Returns: Count on stdout
count_dead_sessions() {
    _dead=0
    if ! state_has_heads; then
        printf '%s' "0"
        return 0
    fi
    while IFS=' ' read -r _branch _session _rest; do
        _mode="$(get_terminal_mode_for_branch "$_branch" 2>/dev/null || echo interactive)"
        if [ "$_mode" != headless ] && [ -n "$_session" ] && ! tmux_session_exists "$_session"; then
            _dead=$((_dead + 1))
        fi
    done <<EOF
$(state_list_heads)
EOF
    printf '%s' "$_dead"
}

# Leftover worktrees from removed tasks, from the same authority as
# 'hydra gc --policy orphaned' (worktree_orphan_rows in worktree_ops.sh), with a
# bounded size. A hydra-<branch> sibling directory name is not evidence.
# Usage: list_orphan_worktree_rows [fresh|cached]
# Output: <clean|dirty>\t<branch>\t<kib|unknown>\t<path>; empty outside a project.
list_orphan_worktree_rows() {
    worktree_orphan_sized_rows "${1:-fresh}" 2>/dev/null || true
}

# Plain-language description of one list_orphan_worktree_rows row.
# Usage: describe_orphan_worktree <clean|dirty> <branch> <kib|unknown> <path>
describe_orphan_worktree() {
    _dow_branch="$2"
    [ "$_dow_branch" != - ] || _dow_branch="detached HEAD"
    if [ "$1" = clean ]; then
        printf '%s (%s); %s; no uncommitted changes\n' "$_dow_branch" "$4" "$(worktree_format_kib "$3")"
    else
        printf '%s (%s); %s; has uncommitted changes\n' "$_dow_branch" "$4" "$(worktree_format_kib "$3")"
    fi
}

# Print each orphan row as a described line after a prefix.
# Usage: print_orphan_worktree_rows <rows> <prefix>
print_orphan_worktree_rows() {
    _powr_tab="$(printf '\t')"
    while IFS="$_powr_tab" read -r _powr_state _powr_branch _powr_kib _powr_path; do
        [ -n "$_powr_path" ] || continue
        printf '%s%s\n' "$2" "$(describe_orphan_worktree "$_powr_state" "$_powr_branch" "$_powr_kib" "$_powr_path")"
    done <<EOF
$1
EOF
}

# One-line count, reclaimable size and change state for orphan rows, e.g.
# "2 leftover worktrees from removed tasks, 1.3 GiB; no uncommitted changes in 2".
# Unmeasured sizes are reported as unknown, never as zero.
# Usage: summarize_orphan_worktrees <rows>
summarize_orphan_worktrees() {
    printf '%s\n' "$1" | awk -F '\t' '
        NF < 4 { next }
        { count++; if ($1 == "clean") clean++; else dirty++ }
        $3 ~ /^[0-9]+$/ { kib += $3; known++; next }
        { unknown++ }
        END {
            printf "%d leftover worktree%s from removed tasks, ", count, (count == 1 ? "" : "s")
            if (!known) printf "size unknown"
            else {
                split("KiB MiB GiB TiB", unit, " "); i = 1; v = kib
                while (v >= 1024 && i < 4) { v /= 1024; i++ }
                if (unknown) printf "at least "
                if (i == 1) printf "%d %s", v, unit[i]; else printf "%.1f %s", v, unit[i]
                if (unknown) printf " (%d size%s unknown)", unknown, (unknown == 1 ? "" : "s")
            }
            if (clean) printf "; no uncommitted changes in %d", clean
            if (dirty) printf "%s uncommitted changes in %d", (clean ? "," : ";"), dirty
            printf "\n"
        }'
}

# Count lock directories with dead same-host owner evidence.
# Usage: count_stale_locks
# Returns: Count on stdout
count_stale_locks() {
    if [ -z "${HYDRA_HOME:-}" ] || [ ! -d "$HYDRA_HOME/locks" ]; then
        printf '%s' "0"
        return 0
    fi
    _stale=0
    while IFS= read -r _lock_dir; do
        [ -n "$_lock_dir" ] || continue
        if lock_dir_is_stale "$_lock_dir"; then
            _stale=$((_stale + 1))
        fi
    done <<EOF
$(find "$HYDRA_HOME/locks" -name "*.lock" -type d 2>/dev/null)
EOF
    printf '%s' "$_stale"
}

# Remove lock directories with dead same-host owner evidence.
# Usage: clean_stale_locks
# Returns: Number removed on stdout
clean_stale_locks() {
    if [ -z "${HYDRA_HOME:-}" ] || [ ! -d "$HYDRA_HOME/locks" ]; then
        printf '%s' "0"
        return 0
    fi
    _cleaned=0
    # Collect paths first; removal rechecks owner evidence to avoid a race.
    _stale_list="$(find "$HYDRA_HOME/locks" -name "*.lock" -type d 2>/dev/null || true)"
    if [ -n "$_stale_list" ]; then
        while IFS= read -r _lock_dir; do
            [ -n "$_lock_dir" ] || continue
            if remove_stale_lock_dir "$_lock_dir" 2>/dev/null; then
                _cleaned=$((_cleaned + 1))
            fi
        done <<EOF
$_stale_list
EOF
    fi
    printf '%s' "$_cleaned"
}

# Mark head records with dead sessions as stopped.
# Returns: Number removed on stdout
clean_dead_heads() {
    _dead_cleaned=0
    if ! state_has_heads; then
        printf '%s' "0"
        return 0
    fi

    _dead_branches=""
    while IFS=' ' read -r _branch _session _ai _group _timestamp _deps _pr; do
        _mode="$(get_terminal_mode_for_branch "$_branch" 2>/dev/null || echo interactive)"
        if [ "$_mode" = headless ] || { [ -n "$_session" ] && tmux_session_exists "$_session"; }; then
            :
        elif state_update_field "$_branch" desired-state stopped; then
            _dead_cleaned=$((_dead_cleaned + 1))
            if [ -n "$_branch" ]; then
                _dead_branches="${_dead_branches}${_branch}
"
            fi
        fi
    done <<EOF
$(state_list_heads)
EOF

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
