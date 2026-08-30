#!/bin/sh
# Hydra 1.7 worktree disk accounting, garbage collection, and repair helpers.

worktree_du_rows() {
    parallel_project_load || return 1
    for _wdr_head in "$PARALLEL_PROJECT_DIR"/heads/head_*; do
        [ -d "$_wdr_head" ] || continue
        _wdr_branch="$(sed -n '1p' "$_wdr_head/branch" 2>/dev/null || true)"
        _wdr_path="$(sed -n '1p' "$_wdr_head/worktree" 2>/dev/null || true)"
        if [ -d "$_wdr_path" ]; then
            _wdr_worktree_kib="$(du -sk "$_wdr_path" 2>/dev/null | awk '{print $1}')"
        else
            _wdr_worktree_kib=0
        fi
        _wdr_state_kib="$(du -sk "$_wdr_head" 2>/dev/null | awk '{print $1}')"
        printf '%s\t%s\t%s\t%s\t%s\n' "$(basename "$_wdr_head")" "$_wdr_branch" "$_wdr_worktree_kib" "$_wdr_state_kib" "$_wdr_path"
    done
}

worktree_path_is_hydra_head() {
    _wpih_path="$1"
    parallel_project_load || return 1
    for _wpih_head in "$PARALLEL_PROJECT_DIR"/heads/head_*; do
        [ -d "$_wpih_head" ] || continue
        [ "$(sed -n '1p' "$_wpih_head/worktree" 2>/dev/null || true)" != "$_wpih_path" ] || return 0
    done
    return 1
}

worktree_gc_orphaned_rows() {
    _wgor_apply="$1"
    _wgor_include_dirty="$2"
    parallel_project_load || return 1
    _wgor_root="$(project_worktree_root "$PARALLEL_PROJECT_ID")" || return 1
    _wgor_repo="$(get_repo_root)" || return 1
    git -C "$_wgor_repo" worktree list --porcelain | sed -n 's/^worktree //p' | while IFS= read -r _wgor_path; do
        [ "$_wgor_path" != "$_wgor_repo" ] || continue
        case "$_wgor_path" in "$_wgor_root"/*) ;; *) continue ;; esac
        worktree_path_is_hydra_head "$_wgor_path" && continue
        _wgor_dirty="$(git -C "$_wgor_path" status --porcelain=v1 2>/dev/null || true)"
        if [ -n "$_wgor_dirty" ] && [ "$_wgor_include_dirty" -eq 0 ]; then
            printf 'preserved-dirty\t%s\n' "$_wgor_path"
            continue
        fi
        if [ "$_wgor_apply" -eq 0 ]; then
            printf 'would-remove-orphan\t%s\n' "$_wgor_path"
        elif [ -n "$_wgor_dirty" ]; then
            if git -C "$_wgor_repo" worktree remove --force "$_wgor_path"; then
                printf 'removed-orphan-dirty\t%s\n' "$_wgor_path"
            else
                printf 'failed\t%s\n' "$_wgor_path"
            fi
        elif git -C "$_wgor_repo" worktree remove "$_wgor_path"; then
            printf 'removed-orphan\t%s\n' "$_wgor_path"
        else
            printf 'failed\t%s\n' "$_wgor_path"
        fi
    done
}

worktree_gc_stopped_rows() {
    _wgsr_apply="$1"
    _wgsr_include_dirty="$2"
    parallel_project_load || return 1
    _wgsr_repo="$(get_repo_root)" || return 1
    for _wgsr_head in "$PARALLEL_PROJECT_DIR"/heads/head_*; do
        [ -d "$_wgsr_head" ] || continue
        [ "$(sed -n '1p' "$_wgsr_head/desired-state" 2>/dev/null || true)" = stopped ] || continue
        _wgsr_path="$(sed -n '1p' "$_wgsr_head/worktree" 2>/dev/null || true)"
        [ -d "$_wgsr_path" ] || continue
        _wgsr_dirty="$(git -C "$_wgsr_path" status --porcelain=v1 2>/dev/null || true)"
        if [ -n "$_wgsr_dirty" ] && [ "$_wgsr_include_dirty" -eq 0 ]; then
            printf 'preserved-dirty\t%s\n' "$_wgsr_path"
        elif [ "$_wgsr_apply" -eq 0 ]; then
            printf 'would-remove-stopped\t%s\n' "$_wgsr_path"
        elif [ -n "$_wgsr_dirty" ] && git -C "$_wgsr_repo" worktree remove --force "$_wgsr_path"; then
            printf 'removed-stopped-dirty\t%s\n' "$_wgsr_path"
        elif [ -z "$_wgsr_dirty" ] && git -C "$_wgsr_repo" worktree remove "$_wgsr_path"; then
            printf 'removed-stopped\t%s\n' "$_wgsr_path"
        else
            printf 'failed\t%s\n' "$_wgsr_path"
        fi
    done
}

worktree_gc_archive_rows() {
    _wgar_apply="$1"
    _wgar_days="$2"
    case "$_wgar_days" in ''|*[!0-9]*) return 1 ;; esac
    parallel_project_load || return 1
    _wgar_now="$(date +%s)"
    _wgar_age=$((_wgar_days * 86400))
    for _wgar_parent in "$PARALLEL_PROJECT_DIR"/heads/head_*/context-packs "$PARALLEL_PROJECT_DIR"/heads/head_*/archives/*; do
        [ -d "$_wgar_parent" ] || continue
        for _wgar_dir in "$_wgar_parent"/*; do
            [ -d "$_wgar_dir" ] || continue
            _wgar_created="$(sed -n '1p' "$_wgar_dir/created-at" 2>/dev/null || true)"
            case "$_wgar_created" in ''|*[!0-9]*) continue ;; esac
            [ $((_wgar_now - _wgar_created)) -ge "$_wgar_age" ] || continue
            if [ "$_wgar_apply" -eq 0 ]; then
                printf 'would-remove-archive\t%s\n' "$_wgar_dir"
            else
                rm -rf "$_wgar_dir"
                printf 'removed-archive\t%s\n' "$_wgar_dir"
            fi
        done
    done
}

worktree_doctor_lock() {
    _wdl_branch="$1"
    _wdl_reason="$2"
    _wdl_dry="$3"
    parallel_head_load "$_wdl_branch" || return 1
    if [ "$_wdl_dry" -eq 1 ]; then
        printf 'would-lock\t%s\n' "$PARALLEL_WORKTREE"
        return 0
    fi
    if [ -n "$_wdl_reason" ]; then
        git -C "$PARALLEL_WORKTREE" worktree lock --reason "$_wdl_reason" "$PARALLEL_WORKTREE"
    else
        git -C "$PARALLEL_WORKTREE" worktree lock "$PARALLEL_WORKTREE"
    fi
}

worktree_doctor_unlock() {
    _wdu_branch="$1"
    _wdu_dry="$2"
    parallel_head_load "$_wdu_branch" || return 1
    if [ "$_wdu_dry" -eq 1 ]; then
        printf 'would-unlock\t%s\n' "$PARALLEL_WORKTREE"
    else
        git -C "$PARALLEL_WORKTREE" worktree unlock "$PARALLEL_WORKTREE"
    fi
}

worktree_doctor_move() {
    _wdm_branch="$1"
    _wdm_target="$2"
    _wdm_dry="$3"
    parallel_head_load "$_wdm_branch" || return 1
    parallel_validate_path_pattern "${_wdm_target#/}" || return 1
    [ ! -e "$_wdm_target" ] || return 1
    _wdm_session="$(sed -n '1p' "$PARALLEL_HEAD_DIR/session")"
    if tmux_session_exists "$_wdm_session"; then
        echo "Error: stop the tmux session before moving its worktree" >&2
        return 1
    fi
    integration_require_clean "$PARALLEL_WORKTREE" "head '$_wdm_branch'" || return 1
    if [ "$_wdm_dry" -eq 1 ]; then
        printf 'would-move\t%s\t%s\n' "$PARALLEL_WORKTREE" "$_wdm_target"
        return 0
    fi
    _wdm_old="$PARALLEL_WORKTREE"
    git -C "$(get_repo_root)" worktree move "$_wdm_old" "$_wdm_target" || return 1
    _wdm_lock="head_${PARALLEL_HEAD_ID}"
    acquire_lock "$_wdm_lock" "worktree path move" "$PARALLEL_HEAD_ID" || {
        git -C "$(get_repo_root)" worktree move "$_wdm_target" "$_wdm_old" >/dev/null 2>&1 || true
        return 1
    }
    if ! state_v2_write_scalar "$PARALLEL_HEAD_DIR/worktree" "$_wdm_target"; then
        release_lock "$_wdm_lock"
        git -C "$(get_repo_root)" worktree move "$_wdm_target" "$_wdm_old" >/dev/null 2>&1 || true
        return 1
    fi
    release_lock "$_wdm_lock"
}

worktree_doctor_repair() {
    _wdr_apply="$1"
    _wdr_repo="$(get_repo_root)" || return 1
    parallel_project_load || return 1
    for _wdr_head in "$PARALLEL_PROJECT_DIR"/heads/head_*; do
        [ -d "$_wdr_head" ] || continue
        _wdr_path="$(sed -n '1p' "$_wdr_head/worktree" 2>/dev/null || true)"
        [ -n "$_wdr_path" ] || continue
        if [ "$_wdr_apply" -eq 0 ]; then
            printf 'would-repair\t%s\n' "$_wdr_path"
        else
            git -C "$_wdr_repo" worktree repair "$_wdr_path" || return 1
            printf 'repaired\t%s\n' "$_wdr_path"
        fi
    done
}

worktree_doctor_prune() {
    _wdp_apply="$1"
    _wdp_repo="$(get_repo_root)" || return 1
    if [ "$_wdp_apply" -eq 0 ]; then
        git -C "$_wdp_repo" worktree prune --dry-run --verbose
    else
        git -C "$_wdp_repo" worktree prune --verbose
    fi
}
