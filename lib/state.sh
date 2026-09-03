#!/bin/sh
# Project-scoped head queries and mutations over authoritative state v2 records.

get_timestamp() {
    date +%s
}

format_duration() {
    _fd_seconds="$1"
    case "$_fd_seconds" in ''|-|*[!0-9]*) printf '%s' -; return 0 ;; esac
    if [ "$_fd_seconds" -lt 60 ]; then
        printf '%ds' "$_fd_seconds"
    elif [ "$_fd_seconds" -lt 3600 ]; then
        printf '%dm' "$((_fd_seconds / 60))"
    elif [ "$_fd_seconds" -lt 86400 ]; then
        printf '%dh %dm' "$((_fd_seconds / 3600))" "$((_fd_seconds % 3600 / 60))"
    else
        printf '%dd %dh' "$((_fd_seconds / 86400))" "$((_fd_seconds % 86400 / 3600))"
    fi
}

get_duration_since() {
    _gds_timestamp="$1"
    case "$_gds_timestamp" in ''|-|*[!0-9]*) printf '0\n'; return 0 ;; esac
    printf '%s\n' "$(($(get_timestamp) - _gds_timestamp))"
}

_state_project_id() {
    hydra_get_project_id 2>/dev/null
}

_state_project_dir() {
    _spd_project="$(_state_project_id)" || return 1
    state_v2_project_dir "$_spd_project"
}

_state_head_dir() {
    _shd_branch="$1"
    _shd_project="$(_state_project_id)" || return 1
    _shd_head="$(state_v2_find_head_by_branch "$_shd_project" "$_shd_branch" 2>/dev/null)" || return 1
    state_v2_head_dir "$_shd_project" "$_shd_head"
}

_state_read_field() {
    _srf_dir="$(_state_head_dir "$1")" || return 1
    sed -n '1p' "$_srf_dir/$2" 2>/dev/null
}

_state_head_is_active() {
    _shia_state="$(sed -n '1p' "$1/desired-state" 2>/dev/null || true)"
    [ "$_shia_state" = running ] || [ "$_shia_state" = stopping ]
}

_state_read_or_dash() {
    _srod_value="$(sed -n '1p' "$1" 2>/dev/null || true)"
    printf '%s' "${_srod_value:--}"
}

# Emits active heads as: branch session profile group created-at dependencies pr
state_list_heads() {
    _slh_project_dir="$(_state_project_dir 2>/dev/null || true)"
    [ -d "$_slh_project_dir/heads" ] || return 0
    for _slh_dir in "$_slh_project_dir"/heads/head_*; do
        [ -d "$_slh_dir" ] || continue
        _state_head_is_active "$_slh_dir" || continue
        _slh_branch="$(sed -n '1p' "$_slh_dir/branch" 2>/dev/null || true)"
        _slh_session="$(sed -n '1p' "$_slh_dir/session" 2>/dev/null || true)"
        if [ -z "$_slh_branch" ] || [ -z "$_slh_session" ]; then
            continue
        fi
        printf '%s %s %s %s %s %s %s\n' \
            "$_slh_branch" "$_slh_session" \
            "$(_state_read_or_dash "$_slh_dir/profile")" \
            "$(_state_read_or_dash "$_slh_dir/group")" \
            "$(_state_read_or_dash "$_slh_dir/created-at")" \
            "$(_state_read_or_dash "$_slh_dir/dependencies")" \
            "$(_state_read_or_dash "$_slh_dir/pr")"
    done
}

state_has_heads() {
    [ -n "$(state_list_heads | sed -n '1p')" ]
}

get_session_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" session; }
get_ai_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" profile; }
get_group_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" group; }
get_timestamp_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" created-at; }
get_deps_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" dependencies; }
get_pr_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" pr; }

get_branch_for_session() {
    _gbfs_session="${1:-}"
    [ -n "$_gbfs_session" ] || return 1
    while IFS=' ' read -r _gbfs_branch _gbfs_seen _gbfs_rest; do
        [ "$_gbfs_seen" = "$_gbfs_session" ] || continue
        printf '%s\n' "$_gbfs_branch"
        return 0
    done <<EOF
$(state_list_heads)
EOF
    return 1
}

_lock_head_state() {
    _lhs_project="$(_state_project_id)" || {
        echo "Error: Project identity is unavailable" >&2
        return 1
    }
    acquire_lock "state_${_lhs_project}" "head state mutation" || {
        echo "Error: Failed to acquire head state lock" >&2
        return 1
    }
    HYDRA_HELD_STATE_LOCK="state_${_lhs_project}"
}

_unlock_head_state() {
    [ -z "${HYDRA_HELD_STATE_LOCK:-}" ] || release_lock "$HYDRA_HELD_STATE_LOCK"
    HYDRA_HELD_STATE_LOCK=""
}

_state_update_field_locked() {
    _sufl_branch="$1"
    _sufl_field="$2"
    _sufl_value="$3"
    case "$_sufl_field" in session|profile|group|created-at|dependencies|pr|desired-state) ;;
        *) return 1 ;;
    esac
    _sufl_dir="$(_state_head_dir "$_sufl_branch")" || {
        echo "Error: Head '$_sufl_branch' is unavailable" >&2
        return 1
    }
    state_v2_write_scalar "$_sufl_dir/$_sufl_field" "$_sufl_value"
}

state_update_field() {
    [ -n "${1:-}" ] || { echo "Error: Branch is required" >&2; return 1; }
    _lock_head_state || return 1
    if ! _state_update_field_locked "$1" "$2" "$3"; then
        _unlock_head_state
        return 1
    fi
    _unlock_head_state
}

set_group() { state_update_field "$1" group "${2:--}"; }
set_deps() { state_update_field "$1" dependencies "${2:--}"; }
set_pr_for_branch() { state_update_field "$1" pr "${2:--}"; }

validate_head_state() {
    _vhs_errors=0
    while IFS=' ' read -r _vhs_branch _vhs_session _vhs_rest; do
        [ -n "$_vhs_branch" ] || continue
        if ! git_branch_exists "$_vhs_branch"; then
            echo "Warning: Branch '$_vhs_branch' no longer exists" >&2
            _vhs_errors=1
        fi
        if ! tmux_session_exists "$_vhs_session"; then
            echo "Warning: Session '$_vhs_session' no longer exists" >&2
            _vhs_errors=1
        fi
    done <<EOF
$(state_list_heads)
EOF
    return "$_vhs_errors"
}

cleanup_head_state() {
    _chs_failed=0
    while IFS=' ' read -r _chs_branch _chs_session _chs_rest; do
        [ -n "$_chs_branch" ] || continue
        if ! git_branch_exists "$_chs_branch" || ! tmux_session_exists "$_chs_session"; then
            state_update_field "$_chs_branch" desired-state stopped || _chs_failed=1
        fi
    done <<EOF
$(state_list_heads)
EOF
    return "$_chs_failed"
}

generate_session_name() {
    _gsn_branch="$1"
    [ -n "$_gsn_branch" ] || { echo "Error: Branch is required" >&2; return 1; }
    _gsn_base="$(printf '%s' "$_gsn_branch" | sed 's/[^a-zA-Z0-9_-]/_/g')"
    if try_lock "$_gsn_base"; then
        if ! tmux has-session -t "$_gsn_base" 2>/dev/null; then
            printf '%s\n' "$_gsn_base"
            return 0
        fi
        release_lock "$_gsn_base"
    fi
    _gsn_number=1
    while [ "$_gsn_number" -le 100 ]; do
        _gsn_candidate="${_gsn_base}_${_gsn_number}"
        if try_lock "$_gsn_candidate"; then
            if ! tmux has-session -t "$_gsn_candidate" 2>/dev/null; then
                printf '%s\n' "$_gsn_candidate"
                return 0
            fi
            release_lock "$_gsn_candidate"
        fi
        _gsn_number=$((_gsn_number + 1))
    done
    _gsn_candidate="${_gsn_base}_$(date +%s 2>/dev/null || printf '%s' "$$")"
    try_lock "$_gsn_candidate" 2>/dev/null || true
    printf '%s\n' "$_gsn_candidate"
}

list_groups() {
    state_list_heads | awk '$4 != "-" { print $4 }' | sort -u
}

state_list_heads_for_group() {
    _slhfg_group="$1"
    [ -n "$_slhfg_group" ] || return 0
    state_list_heads | awk -v group="$_slhfg_group" '$4 == group'
}
