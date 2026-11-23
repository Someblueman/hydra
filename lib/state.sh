#!/bin/sh
# Project-scoped head queries and mutations over authoritative state v2 records.

get_timestamp() {
    date +%s
}

# Sets STATE_DURATION to a duration in seconds as short text ("5m", "2h 3m").
state_load_duration() {
    _fd_seconds="$1"
    case "$_fd_seconds" in ''|-|*[!0-9]*) STATE_DURATION=-; return 0 ;; esac
    if [ "$_fd_seconds" -lt 60 ]; then
        STATE_DURATION="$((_fd_seconds))s"
    elif [ "$_fd_seconds" -lt 3600 ]; then
        STATE_DURATION="$((_fd_seconds / 60))m"
    elif [ "$_fd_seconds" -lt 86400 ]; then
        STATE_DURATION="$((_fd_seconds / 3600))h $((_fd_seconds % 3600 / 60))m"
    else
        STATE_DURATION="$((_fd_seconds / 86400))d $((_fd_seconds % 86400 / 3600))h"
    fi
}

format_duration() {
    state_load_duration "$1"
    printf '%s' "$STATE_DURATION"
}

# Sets STATE_AGE_SECONDS to the seconds from <timestamp> to <now>; 0 when the
# timestamp is not a number. A loop reads the clock once and passes it here.
# Usage: state_load_age <timestamp> <now>
state_load_age() {
    _gds_timestamp="$1"
    case "$_gds_timestamp" in ''|-|*[!0-9]*) STATE_AGE_SECONDS=0; return 0 ;; esac
    STATE_AGE_SECONDS="$(($2 - _gds_timestamp))"
}

get_duration_since() {
    state_load_age "$1" "$(get_timestamp)"
    printf '%s\n' "$STATE_AGE_SECONDS"
}

_state_project_id() {
    hydra_get_project_id 2>/dev/null
}

_state_project_dir() {
    _spd_project="$(_state_project_id)" || return 1
    state_v2_project_dir "$_spd_project"
}

# Sets STATE_PROJECT_ID and STATE_PROJECT_DIR for this repository's project.
# Prints nothing; returns 1 outside an initialised project.
_state_load_project() {
    STATE_PROJECT_ID=
    hydra_load_project_id 2>/dev/null || return 1
    STATE_PROJECT_ID="$HYDRA_LOADED_PROJECT_ID"
    hydra_valid_id "$STATE_PROJECT_ID" || return 1
    STATE_PROJECT_DIR="$HYDRA_STATE_V2_ROOT/projects/$STATE_PROJECT_ID"
}

# Sets STATE_HEAD_DIR to the record of a branch's head. Prints nothing;
# returns 1 (and leaves it empty) when the branch has no head.
_state_load_head_dir() {
    STATE_HEAD_DIR=
    _state_load_project || return 1
    state_v2_lookup_head "$STATE_PROJECT_ID" "$1" || return 1
    hydra_valid_id "$STATE_V2_HEAD_ID" || return 1
    STATE_HEAD_DIR="$STATE_V2_HEAD_DIR"
}

_state_head_dir() {
    _state_load_head_dir "$1" || return 1
    printf '%s\n' "$STATE_HEAD_DIR"
}

_state_read_field() {
    _state_load_head_dir "$1" || return 1
    if [ ! -f "$STATE_HEAD_DIR/$2" ] || [ ! -r "$STATE_HEAD_DIR/$2" ]; then return 1; fi
    state_v2_read_scalar "$STATE_HEAD_DIR/$2"
    _srf_value="$STATE_V2_SCALAR"
    printf '%s\n' "$_srf_value"
}

_state_head_is_active() {
    state_v2_read_scalar "$1/desired-state"
    _shia_state="$STATE_V2_SCALAR"
    [ "$_shia_state" = running ] || [ "$_shia_state" = headless ] || [ "$_shia_state" = stopping ]
}

# True for a record that state_list_heads prints: an active head with a
# branch and a session. Sets STATE_ROW_BRANCH and STATE_ROW_SESSION.
_state_head_listed() {
    [ -d "$1" ] || return 1
    _state_head_is_active "$1" || return 1
    state_v2_read_scalar "$1/branch"
    STATE_ROW_BRANCH="$STATE_V2_SCALAR"
    state_v2_read_scalar "$1/session"
    STATE_ROW_SESSION="$STATE_V2_SCALAR"
    [ -n "$STATE_ROW_BRANCH" ] && [ -n "$STATE_ROW_SESSION" ]
}

# Emits active heads as: branch session profile group created-at dependencies pr
state_list_heads() {
    _state_load_project 2>/dev/null || return 0
    [ -d "$STATE_PROJECT_DIR/heads" ] || return 0
    for _slh_dir in "$STATE_PROJECT_DIR"/heads/head_*; do
        _state_head_listed "$_slh_dir" || continue
        state_v2_read_scalar "$_slh_dir/profile"
        _slh_profile="$STATE_V2_SCALAR"
        state_v2_read_scalar "$_slh_dir/group"
        _slh_group="$STATE_V2_SCALAR"
        state_v2_read_scalar "$_slh_dir/created-at"
        _slh_created="$STATE_V2_SCALAR"
        state_v2_read_scalar "$_slh_dir/dependencies"
        _slh_deps="$STATE_V2_SCALAR"
        state_v2_read_scalar "$_slh_dir/pr"
        _slh_pr="$STATE_V2_SCALAR"
        printf '%s %s %s %s %s %s %s\n' \
            "$STATE_ROW_BRANCH" "$STATE_ROW_SESSION" \
            "${_slh_profile:--}" "${_slh_group:--}" "${_slh_created:--}" \
            "${_slh_deps:--}" "${_slh_pr:--}"
    done
}

state_has_heads() {
    _state_load_project 2>/dev/null || return 1
    [ -d "$STATE_PROJECT_DIR/heads" ] || return 1
    for _shh_dir in "$STATE_PROJECT_DIR"/heads/head_*; do
        _state_head_listed "$_shh_dir" && return 0
    done
    return 1
}

# Sets STATE_TERMINAL_MODE to the terminal mode of a branch's head. Prints
# nothing; returns 1 when the branch has no head.
_state_load_terminal_mode() {
    [ -n "${1:-}" ] || return 1
    _state_load_head_dir "$1" || return 1
    state_v2_read_scalar "$STATE_HEAD_DIR/terminal-mode"
    STATE_TERMINAL_MODE="$STATE_V2_SCALAR"
    # State v2 records written before optional-terminal support are interactive.
    case "$STATE_TERMINAL_MODE" in headless|interactive) ;; *) STATE_TERMINAL_MODE=interactive ;; esac
}

state_has_interactive_heads() {
    _state_load_project 2>/dev/null || return 1
    [ -d "$STATE_PROJECT_DIR/heads" ] || return 1
    for _shih_dir in "$STATE_PROJECT_DIR"/heads/head_*; do
        _state_head_listed "$_shih_dir" || continue
        # The mode is that of the record the branch resolves to, as for
        # every other lookup by branch.
        _state_load_terminal_mode "$STATE_ROW_BRANCH" || return 0
        [ "$STATE_TERMINAL_MODE" != interactive ] || return 0
    done
    return 1
}

get_session_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" session; }
get_terminal_mode_for_branch() {
    _state_load_terminal_mode "${1:-}" || return 1
    printf '%s\n' "$STATE_TERMINAL_MODE"
}
get_group_for_branch() { [ -n "${1:-}" ] && _state_read_field "$1" group; }
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
    _state_load_head_dir "$_sufl_branch" || {
        echo "Error: Head '$_sufl_branch' is unavailable" >&2
        return 1
    }
    state_v2_write_scalar "$STATE_HEAD_DIR/$_sufl_field" "$_sufl_value"
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
set_pr_for_branch() { state_update_field "$1" pr "${2:--}"; }

generate_session_name() {
    _gsn_branch="$1"
    [ -n "$_gsn_branch" ] || { echo "Error: Branch is required" >&2; return 1; }
    _gsn_base="$(printf '%s' "$_gsn_branch" | sed 's/[^a-zA-Z0-9_-]/_/g')"
    # "=name:" is tmux's exact session match; a bare name would also report a
    # longer session it is a prefix of. (Inline: this file loads without
    # lib/tmux.sh and its tmux_exact_target for some commands.)
    if try_lock "$_gsn_base"; then
        if ! tmux has-session -t "=$_gsn_base:" 2>/dev/null; then
            printf '%s\n' "$_gsn_base"
            return 0
        fi
        release_lock "$_gsn_base"
    fi
    _gsn_number=1
    while [ "$_gsn_number" -le 100 ]; do
        _gsn_candidate="${_gsn_base}_${_gsn_number}"
        if try_lock "$_gsn_candidate"; then
            if ! tmux has-session -t "=$_gsn_candidate:" 2>/dev/null; then
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
