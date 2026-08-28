#!/bin/sh
# Out-of-band execution, Git evidence, and per-head provenance.

operations_append_head() {
    _oah_file="$1"
    _oah_branch="$2"
    lifecycle_load_head "$_oah_branch" || return 1
    _oah_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree" 2>/dev/null || true)"
    [ -n "$_oah_worktree" ] && [ -d "$_oah_worktree" ] || {
        echo "Error: worktree for '$_oah_branch' is unavailable" >&2
        return 1
    }
    if grep -Fxq "$LIFECYCLE_HEAD_ID" "$_oah_file" 2>/dev/null; then
        echo "Error: head '$_oah_branch' was selected more than once" >&2
        return 1
    fi
    printf '%s\n' "$LIFECYCLE_HEAD_ID" >> "$_oah_file"
}

operations_load_selected_head() {
    _olsh_head="$1"
    hydra_valid_id "$_olsh_head" || return 1
    _olsh_dir="$HYDRA_STATE_V2_ROOT/projects/$LIFECYCLE_PROJECT_ID/heads/$_olsh_head"
    [ -d "$_olsh_dir" ] || return 1
    OPERATIONS_HEAD_ID="$_olsh_head"
    OPERATIONS_BRANCH="$(sed -n '1p' "$_olsh_dir/branch")"
    OPERATIONS_WORKTREE="$(sed -n '1p' "$_olsh_dir/worktree")"
    OPERATIONS_BASE="$(sed -n '1p' "$_olsh_dir/base-ref")"
    [ -n "$OPERATIONS_BRANCH" ] && [ -d "$OPERATIONS_WORKTREE" ] && [ -n "$OPERATIONS_BASE" ] || return 1
    export OPERATIONS_HEAD_ID OPERATIONS_BRANCH OPERATIONS_WORKTREE OPERATIONS_BASE
}

operations_select_heads() {
    _osh_file="$1"
    _osh_branches="$2"
    _osh_group="$3"
    _osh_all="$4"
    : > "$_osh_file"
    if [ -n "$_osh_branches" ]; then
        while IFS= read -r _osh_branch; do
            operations_append_head "$_osh_file" "$_osh_branch" || return 1
        done <<EOF
$_osh_branches
EOF
    elif [ -n "$_osh_group" ] || [ "$_osh_all" -eq 1 ]; then
        _osh_project="$(hydra_get_project_id)" || return 1
        _osh_project_dir="$(state_v2_project_dir "$_osh_project")" || return 1
        for _osh_head_dir in "$_osh_project_dir"/heads/head_*; do
            [ -d "$_osh_head_dir" ] || continue
            _osh_head_group="$(sed -n '1p' "$_osh_head_dir/group" 2>/dev/null || true)"
            [ -z "$_osh_group" ] || [ "$_osh_head_group" = "$_osh_group" ] || continue
            _osh_branch="$(sed -n '1p' "$_osh_head_dir/branch" 2>/dev/null || true)"
            operations_append_head "$_osh_file" "$_osh_branch" || return 1
        done
    else
        _osh_branch="$(git branch --show-current 2>/dev/null || true)"
        [ -n "$_osh_branch" ] || { echo "Error: select a branch, group, or --all" >&2; return 1; }
        operations_append_head "$_osh_file" "$_osh_branch" || return 1
    fi
    [ -s "$_osh_file" ] || { echo "Error: selection contains no available worktrees" >&2; return 1; }
}

operations_signal_tree() (
    _ost_pid="$1"
    _ost_signal="$2"
    for _ost_child in $(ps -eo pid=,ppid= 2>/dev/null | awk -v parent="$_ost_pid" '$2 == parent { print $1 }'); do
        operations_signal_tree "$_ost_child" "$_ost_signal"
    done
    kill "-$_ost_signal" "$_ost_pid" 2>/dev/null || true
)

operations_exec_worker() {
    _oew_run="$1"
    _oew_head="$2"
    _oew_branch="$3"
    _oew_worktree="$4"
    _oew_timeout="$5"
    _oew_max="$6"
    shift 6
    _oew_dir="$HYDRA_STATE_V2_ROOT/projects/$LIFECYCLE_PROJECT_ID/exec/$_oew_run/$_oew_head"
    mkdir -p "$_oew_dir" || return 0
    chmod 700 "$_oew_dir" 2>/dev/null || true
    _oew_stdout="$_oew_dir/.stdout.full"
    _oew_stderr="$_oew_dir/.stderr.full"
    _oew_timed="$_oew_dir/.timed-out"
    _oew_started="$(date +%s)"
    (cd "$_oew_worktree" && exec "$@") > "$_oew_stdout" 2> "$_oew_stderr" &
    _oew_pid=$!
    _oew_watchdog=""
    if [ "$_oew_timeout" -gt 0 ]; then
        (
            _oew_timer=""
            trap '[ -z "$_oew_timer" ] || kill "$_oew_timer" 2>/dev/null; exit 0' TERM HUP INT
            sleep "$_oew_timeout" &
            _oew_timer=$!
            wait "$_oew_timer" || exit 0
            if kill -0 "$_oew_pid" 2>/dev/null; then
                : > "$_oew_timed"
                operations_signal_tree "$_oew_pid" TERM
                sleep 1
                operations_signal_tree "$_oew_pid" KILL
            fi
        ) >/dev/null 2>&1 &
        _oew_watchdog=$!
    fi
    if wait "$_oew_pid"; then _oew_wait_status=0; else _oew_wait_status=$?; fi
    if [ -n "$_oew_watchdog" ]; then
        kill "$_oew_watchdog" 2>/dev/null || true
        wait "$_oew_watchdog" 2>/dev/null || true
    fi
    if [ -f "$_oew_timed" ]; then _oew_status=124; else _oew_status="$_oew_wait_status"; fi
    tail -c "$_oew_max" "$_oew_stdout" > "$_oew_dir/stdout" 2>/dev/null || : > "$_oew_dir/stdout"
    tail -c "$_oew_max" "$_oew_stderr" > "$_oew_dir/stderr" 2>/dev/null || : > "$_oew_dir/stderr"
    printf '%s\n' "$_oew_status" > "$_oew_dir/status"
    printf '%s\n' "$_oew_branch" > "$_oew_dir/branch"
    printf '%s\n' "$_oew_started" > "$_oew_dir/started-at"
    printf '%s\n' "$(date +%s)" > "$_oew_dir/finished-at"
    printf '%s\n' "$(printf '%s\000' "$@" | hydra_hash)" > "$_oew_dir/argv-hash"
    chmod 600 "$_oew_dir/stdout" "$_oew_dir/stderr" "$_oew_dir/status" \
        "$_oew_dir/branch" "$_oew_dir/started-at" "$_oew_dir/finished-at" "$_oew_dir/argv-hash" 2>/dev/null || true
    : > "$_oew_dir/complete"
    chmod 600 "$_oew_dir/complete" 2>/dev/null || true
    rm -f "$_oew_stdout" "$_oew_stderr" "$_oew_timed"
    _oew_instance="$(sed -n '1p' "$HYDRA_STATE_V2_ROOT/projects/$LIFECYCLE_PROJECT_ID/heads/$_oew_head/current-instance" 2>/dev/null || true)"
    if hydra_valid_id "$_oew_instance"; then
        event_emit "$LIFECYCLE_PROJECT_ID" "$_oew_head" "$_oew_instance" exec.completed hydra local \
            "{\"run_id\":\"$_oew_run\",\"exit_code\":$_oew_status}" >/dev/null 2>&1 || true
    fi
}

operations_git_counts() {
    _ogc_worktree="$1"
    _ogc_base="$2"
    _ogc_counts="$(git -C "$_ogc_worktree" rev-list --left-right --count "$_ogc_base...HEAD" 2>/dev/null || echo '0 0')"
    OPERATIONS_BEHIND="${_ogc_counts%%[[:space:]]*}"
    OPERATIONS_AHEAD="${_ogc_counts##*[[:space:]]}"
    OPERATIONS_DIRTY="$(git -C "$_ogc_worktree" status --porcelain=v1 2>/dev/null | wc -l | tr -d ' ')"
    export OPERATIONS_BEHIND OPERATIONS_AHEAD OPERATIONS_DIRTY
}

provenance_capture_instance() {
    _pci_branch="$1"
    _pci_mode="$2"
    _pci_recipe="$3"
    lifecycle_load_head "$_pci_branch" || return 1
    _pci_profile="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/profile" 2>/dev/null || true)"
    _pci_executable="$(profile_executable_path "${_pci_profile:-none}" 2>/dev/null || true)"
    _pci_version=""
    if profile_builtin_exists "${_pci_profile:-none}" && [ -n "$_pci_executable" ] && [ "$_pci_executable" != none ]; then
        _pci_version="$("$_pci_executable" --version 2>/dev/null | sed -n '1p' || true)"
    elif [ -n "$_pci_executable" ] && [ "$_pci_executable" != none ]; then
        _pci_version=user-declared
    fi
    _pci_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_pci_lock" "record instance provenance" "$LIFECYCLE_HEAD_ID" || return 1
    if ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/provenance-mode" "$_pci_mode" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/resolved-profile" "${_pci_profile:-none}" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/profile-executable" "${_pci_executable:-none}" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/profile-version" "${_pci_version:-unavailable}" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/hydra-version" "$HYDRA_VERSION" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/resolved-recipe" "$_pci_recipe"; then
        release_lock "$_pci_lock"
        return 1
    fi
    release_lock "$_pci_lock"
}

provenance_capture_head() {
    _pch_branch="$1"
    lifecycle_load_head "$_pch_branch" || return 1
    _pch_dir="$LIFECYCLE_HEAD_DIR/provenance"
    _pch_task="$LIFECYCLE_HEAD_DIR/task"
    _pch_trust="$(project_config_hash 2>/dev/null || echo unavailable)"
    _pch_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_pch_lock" "record head provenance" "$LIFECYCLE_HEAD_ID" || return 1
    mkdir -p "$_pch_dir" || { release_lock "$_pch_lock"; return 1; }
    chmod 700 "$_pch_dir" 2>/dev/null || true
    if ! state_v2_write_scalar "$_pch_dir/hydra-version" "$HYDRA_VERSION" || \
       ! state_v2_write_scalar "$_pch_dir/git-version" "$(git --version 2>/dev/null | sed -n '1p')" || \
       ! state_v2_write_scalar "$_pch_dir/tmux-version" "$(tmux -V 2>/dev/null | sed -n '1p')" || \
       ! state_v2_write_scalar "$_pch_dir/base-ref" "$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/base-ref")" || \
       ! state_v2_write_scalar "$_pch_dir/task-hash" "$(hydra_hash < "$_pch_task")" || \
       ! state_v2_write_scalar "$_pch_dir/task-bytes" "$(LC_ALL=C wc -c < "$_pch_task" | tr -d ' ')" || \
       ! state_v2_write_scalar "$_pch_dir/trusted-config-hash" "$_pch_trust" || \
       ! state_v2_write_scalar "$_pch_dir/lifecycle-sources" "hydra,generic-ingest"; then
        release_lock "$_pch_lock"
        return 1
    fi
    release_lock "$_pch_lock"
    event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" provenance.recorded hydra local '{}' >/dev/null
}
