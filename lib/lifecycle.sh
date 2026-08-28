#!/bin/sh
# Instance-scoped lifecycle state and durable evidence checks.

lifecycle_load_head() {
    _llh_branch="$1"
    LIFECYCLE_PROJECT_ID="$(hydra_get_project_id 2>/dev/null)" || return 1
    LIFECYCLE_HEAD_ID="$(state_v2_find_head_by_branch "$LIFECYCLE_PROJECT_ID" "$_llh_branch" 2>/dev/null)" || {
        echo "Error: no Hydra head for branch '$_llh_branch'" >&2
        return 1
    }
    LIFECYCLE_HEAD_DIR="$(state_v2_head_dir "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID")" || return 1
    LIFECYCLE_INSTANCE_ID="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/current-instance" 2>/dev/null || true)"
    hydra_valid_id "$LIFECYCLE_INSTANCE_ID" || {
        echo "Error: head '$_llh_branch' has no valid current instance" >&2
        return 1
    }
    LIFECYCLE_INSTANCE_DIR="$LIFECYCLE_HEAD_DIR/instances/$LIFECYCLE_INSTANCE_ID"
    [ -d "$LIFECYCLE_INSTANCE_DIR" ] || return 1
    LIFECYCLE_BRANCH="$_llh_branch"
    export LIFECYCLE_PROJECT_ID LIFECYCLE_HEAD_ID LIFECYCLE_HEAD_DIR
    export LIFECYCLE_INSTANCE_ID LIFECYCLE_INSTANCE_DIR LIFECYCLE_BRANCH
}

lifecycle_read() {
    _lr_path="$1"
    sed -n '1p' "$LIFECYCLE_INSTANCE_DIR/$_lr_path" 2>/dev/null || true
}

lifecycle_write_head_scalar() {
    _lwhs_branch="$1"
    _lwhs_name="$2"
    _lwhs_value="$3"
    case "$_lwhs_name" in ''|*[!a-z0-9-]*) return 1 ;; esac
    lifecycle_load_head "$_lwhs_branch" || return 1
    _lwhs_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_lwhs_lock" "update lifecycle head" "$LIFECYCLE_HEAD_ID" || return 1
    if ! state_v2_write_scalar "$LIFECYCLE_HEAD_DIR/$_lwhs_name" "$_lwhs_value"; then
        release_lock "$_lwhs_lock"
        return 1
    fi
    release_lock "$_lwhs_lock"
}

lifecycle_write_instance_scalar() {
    _lwis_branch="$1"
    _lwis_name="$2"
    _lwis_value="$3"
    case "$_lwis_name" in ''|*[!a-z0-9-]*) return 1 ;; esac
    lifecycle_load_head "$_lwis_branch" || return 1
    _lwis_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_lwis_lock" "update lifecycle instance" "$LIFECYCLE_HEAD_ID" || return 1
    if ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/$_lwis_name" "$_lwis_value"; then
        release_lock "$_lwis_lock"
        return 1
    fi
    release_lock "$_lwis_lock"
}

lifecycle_set_outcome() {
    _lso_branch="$1"
    _lso_outcome="$2"
    _lso_actor_kind="${3:-human}"
    _lso_actor_id="${4:-local}"
    _lso_summary="${5:-}"
    case "$_lso_outcome" in done|failed|blocked|abandoned|canceled) ;; *) return 1 ;; esac
    case "$_lso_actor_kind" in human|agent|adapter|hydra) ;; *) return 1 ;; esac
    lifecycle_load_head "$_lso_branch" || return 1
    _lso_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_lso_lock" "declare lifecycle outcome" "$LIFECYCLE_HEAD_ID" || return 1
    _lso_now="$(date +%s)"
    if ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/declared-outcome" "$_lso_outcome" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/outcome-actor-kind" "$_lso_actor_kind" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/outcome-actor-id" "$_lso_actor_id" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/outcome-at" "$_lso_now" || \
       ! state_v2_write_text "$LIFECYCLE_INSTANCE_DIR/outcome-summary" "$_lso_summary"; then
        release_lock "$_lso_lock"
        return 1
    fi
    release_lock "$_lso_lock"
    event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" \
        lifecycle.declared "$_lso_actor_kind" "$_lso_actor_id" \
        "{\"outcome\":\"$(json_escape "$_lso_outcome")\"}" >/dev/null
}

lifecycle_set_observed() {
    _lso_branch="$1"
    _lso_status="$2"
    _lso_source="${3:-hydra}"
    _lso_confidence="${4:-exact}"
    _lso_exit_code="${5:-}"
    case "$_lso_status" in starting|running|idle|exited|failed|unavailable) ;; *) return 1 ;; esac
    case "$_lso_confidence" in exact|reported|inferred|unavailable) ;; *) return 1 ;; esac
    case "$_lso_exit_code" in ''|*[!0-9]*) [ -z "$_lso_exit_code" ] || return 1 ;; esac
    lifecycle_load_head "$_lso_branch" || return 1
    _lso_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_lso_lock" "record observed lifecycle" "$LIFECYCLE_HEAD_ID" || return 1
    _lso_now="$(date +%s)"
    if ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/observed-status" "$_lso_status" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/observed-source" "$_lso_source" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/observed-confidence" "$_lso_confidence" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/observed-at" "$_lso_now" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/observed-exit-code" "$_lso_exit_code"; then
        release_lock "$_lso_lock"
        return 1
    fi
    release_lock "$_lso_lock"
    _lso_exit_json=null
    [ -z "$_lso_exit_code" ] || _lso_exit_json="$_lso_exit_code"
    event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" \
        lifecycle.observed adapter "$_lso_source" \
        "{\"status\":\"$(json_escape "$_lso_status")\",\"confidence\":\"$(json_escape "$_lso_confidence")\",\"exit_code\":$_lso_exit_json}" >/dev/null
}

lifecycle_liveness() {
    _ll_branch="$1"
    lifecycle_load_head "$_ll_branch" || return 1
    _ll_session="$(sed -n '1p' "$LIFECYCLE_INSTANCE_DIR/session" 2>/dev/null || true)"
    if [ -n "$_ll_session" ] && tmux has-session -t "$_ll_session" 2>/dev/null; then
        printf 'live\n'
    else
        printf 'stopped\n'
    fi
}

lifecycle_snapshot() {
    _ls_branch="$1"
    LIFECYCLE_SNAPSHOT_INSTANCE=""
    LIFECYCLE_SNAPSHOT_OUTCOME=""
    LIFECYCLE_SNAPSHOT_OBSERVED="unavailable"
    LIFECYCLE_SNAPSHOT_CONFIDENCE="unavailable"
    LIFECYCLE_SNAPSHOT_LIVENESS="unavailable"
    LIFECYCLE_SNAPSHOT_POLICY=""
    LIFECYCLE_SNAPSHOT_COMPLETE=false
    if ! lifecycle_load_head "$_ls_branch" 2>/dev/null; then
        return 0
    fi
    LIFECYCLE_SNAPSHOT_INSTANCE="$LIFECYCLE_INSTANCE_ID"
    LIFECYCLE_SNAPSHOT_OUTCOME="$(lifecycle_read declared-outcome)"
    LIFECYCLE_SNAPSHOT_OBSERVED="$(lifecycle_read observed-status)"
    LIFECYCLE_SNAPSHOT_CONFIDENCE="$(lifecycle_read observed-confidence)"
    LIFECYCLE_SNAPSHOT_LIVENESS="$(lifecycle_liveness "$_ls_branch" 2>/dev/null || echo unavailable)"
    LIFECYCLE_SNAPSHOT_POLICY="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/completion-policy" 2>/dev/null || true)"
    if lifecycle_completion_satisfied "$_ls_branch"; then LIFECYCLE_SNAPSHOT_COMPLETE=true; fi
    export LIFECYCLE_SNAPSHOT_INSTANCE LIFECYCLE_SNAPSHOT_OUTCOME
    export LIFECYCLE_SNAPSHOT_OBSERVED LIFECYCLE_SNAPSHOT_CONFIDENCE
    export LIFECYCLE_SNAPSHOT_LIVENESS LIFECYCLE_SNAPSHOT_POLICY LIFECYCLE_SNAPSHOT_COMPLETE
}

lifecycle_completion_satisfied() {
    _lcs_branch="$1"
    lifecycle_load_head "$_lcs_branch" || return 1
    _lcs_policy="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/completion-policy" 2>/dev/null || true)"
    _lcs_outcome="$(lifecycle_read declared-outcome)"
    _lcs_observed="$(lifecycle_read observed-status)"
    _lcs_exit="$(lifecycle_read observed-exit-code)"
    case "${_lcs_policy:-declared-done}" in
        declared|declared-done) [ "$_lcs_outcome" = "done" ] ;;
        observed-exit-zero) [ "$_lcs_observed" = exited ] && [ "$_lcs_exit" = 0 ] ;;
        either) [ "$_lcs_outcome" = "done" ] || { [ "$_lcs_observed" = exited ] && [ "$_lcs_exit" = 0 ]; } ;;
        *) return 1 ;;
    esac
}

lifecycle_condition_met() {
    _lcm_branch="$1"
    _lcm_condition="${2:-complete}"
    lifecycle_load_head "$_lcm_branch" || return 1
    case "$_lcm_condition" in
        complete) lifecycle_completion_satisfied "$_lcm_branch" ;;
        outcome=terminal)
            case "$(lifecycle_read declared-outcome)" in done|failed|blocked|abandoned|canceled) return 0 ;; *) return 1 ;; esac
            ;;
        outcome=*) [ "$(lifecycle_read declared-outcome)" = "${_lcm_condition#outcome=}" ] ;;
        observed=*) [ "$(lifecycle_read observed-status)" = "${_lcm_condition#observed=}" ] ;;
        liveness=*) [ "$(lifecycle_liveness "$_lcm_branch")" = "${_lcm_condition#liveness=}" ] ;;
        *) echo "Error: unsupported lifecycle condition '$_lcm_condition'" >&2; return 2 ;;
    esac
}

lifecycle_new_instance() {
    _lni_branch="$1"
    _lni_session="$2"
    _lni_provider="$3"
    _lni_recipe="$4"
    lifecycle_load_head "$_lni_branch" || return 1
    _lni_previous="$LIFECYCLE_INSTANCE_ID"
    _lni_new="$(hydra_new_id instance "$LIFECYCLE_HEAD_ID|$_lni_session|resume")" || return 1
    _lni_new_dir="$LIFECYCLE_HEAD_DIR/instances/$_lni_new"
    _lni_lock="state_${LIFECYCLE_PROJECT_ID}"
    acquire_lock "$_lni_lock" "create lifecycle instance" "$LIFECYCLE_HEAD_ID" || return 1
    mkdir -p "$_lni_new_dir" || { release_lock "$_lni_lock"; return 1; }
    chmod 700 "$_lni_new_dir" 2>/dev/null || true
    _lni_now="$(date +%s)"
    if ! state_v2_write_scalar "$_lni_new_dir/instance-id" "$_lni_new" || \
       ! state_v2_write_scalar "$_lni_new_dir/session" "$_lni_session" || \
       ! state_v2_write_scalar "$_lni_new_dir/started-at" "$_lni_now" || \
       ! state_v2_write_scalar "$_lni_new_dir/provider-session-id" "$_lni_provider" || \
       ! state_v2_write_scalar "$_lni_new_dir/resume-recipe" "$_lni_recipe" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/superseded-by" "$_lni_new" || \
       ! state_v2_write_scalar "$LIFECYCLE_INSTANCE_DIR/ended-at" "$_lni_now" || \
       ! state_v2_write_scalar "$LIFECYCLE_HEAD_DIR/current-instance" "$_lni_new" || \
       ! state_v2_write_scalar "$LIFECYCLE_HEAD_DIR/session" "$_lni_session" || \
       ! state_v2_write_scalar "$LIFECYCLE_HEAD_DIR/desired-state" running; then
        release_lock "$_lni_lock"
        return 1
    fi
    release_lock "$_lni_lock"
    LIFECYCLE_PREVIOUS_INSTANCE="$_lni_previous"
    LIFECYCLE_INSTANCE_ID="$_lni_new"
    LIFECYCLE_INSTANCE_DIR="$_lni_new_dir"
    export LIFECYCLE_PREVIOUS_INSTANCE LIFECYCLE_INSTANCE_ID LIFECYCLE_INSTANCE_DIR
}

lifecycle_archive_transcript() {
    _lat_branch="$1"
    _lat_session="$2"
    _lat_policy="${3:-none}"
    case "$_lat_policy" in none) return 0 ;; redacted|full) ;; *) return 1 ;; esac
    lifecycle_load_head "$_lat_branch" || return 1
    _lat_max="${HYDRA_TRANSCRIPT_MAX_BYTES:-1048576}"
    _lat_keep="${HYDRA_TRANSCRIPT_KEEP:-10}"
    case "$_lat_max:$_lat_keep" in *[!0-9:]*) return 1 ;; esac
    _lat_dir="$LIFECYCLE_HEAD_DIR/transcripts"
    mkdir -p "$_lat_dir" || return 1
    chmod 700 "$_lat_dir" 2>/dev/null || true
    _lat_tmp="$(mktemp_adjacent "$_lat_dir/$LIFECYCLE_INSTANCE_ID.txt")" || return 1
    if [ "$_lat_policy" = redacted ]; then
        tmux capture-pane -p -S -2000 -t "$_lat_session:0.0" 2>/dev/null | \
            sed -e 's/\([A-Za-z_][A-Za-z0-9_]*TOKEN=\)[^ ]*/\1[REDACTED]/g' \
                -e 's/\([A-Za-z_][A-Za-z0-9_]*SECRET=\)[^ ]*/\1[REDACTED]/g' \
                -e 's/\([A-Za-z_][A-Za-z0-9_]*PASSWORD=\)[^ ]*/\1[REDACTED]/g' \
                -e 's/\([A-Za-z_][A-Za-z0-9_]*KEY=\)[^ ]*/\1[REDACTED]/g' \
                -e 's/sk-[A-Za-z0-9_-][A-Za-z0-9_-]*/[REDACTED]/g' | tail -c "$_lat_max" > "$_lat_tmp"
    else
        tmux capture-pane -p -S -2000 -t "$_lat_session:0.0" 2>/dev/null | tail -c "$_lat_max" > "$_lat_tmp"
    fi
    chmod 600 "$_lat_tmp" 2>/dev/null || true
    atomic_replace "$_lat_dir/$LIFECYCLE_INSTANCE_ID.txt" "$_lat_tmp" || return 1
    lifecycle_write_instance_scalar "$_lat_branch" transcript-policy "$_lat_policy" || return 1
    lifecycle_write_instance_scalar "$_lat_branch" transcript-path "$LIFECYCLE_HEAD_DIR/transcripts/$LIFECYCLE_INSTANCE_ID.txt" || return 1
    _lat_count="$(find "$_lat_dir" -type f -name 'instance_*.txt' | wc -l | tr -d ' ')"
    while [ "$_lat_count" -gt "$_lat_keep" ]; do
        # Filenames are validated opaque instance IDs; mtime ordering implements retention.
        # shellcheck disable=SC2012
        _lat_oldest="$(ls -1t "$_lat_dir"/instance_*.txt 2>/dev/null | tail -n 1)"
        [ -n "$_lat_oldest" ] || break
        rm -f "$_lat_oldest"
        _lat_count=$((_lat_count - 1))
    done
}

lifecycle_prepare_teardown() {
    _lpt_branch="$1"
    _lpt_session="$2"
    _lpt_policy="${3:-none}"
    lifecycle_load_head "$_lpt_branch" || return 0
    _lpt_repo="$(sed -n '1p' "$(state_v2_project_dir "$LIFECYCLE_PROJECT_ID")/repo-root" 2>/dev/null || true)"
    _lpt_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree" 2>/dev/null || true)"
    run_hook pre-teardown "$_lpt_worktree" "$_lpt_repo" "$_lpt_session" "$_lpt_branch"
    lifecycle_archive_transcript "$_lpt_branch" "$_lpt_session" "$_lpt_policy" || return 1
    lifecycle_write_head_scalar "$_lpt_branch" desired-state stopping || return 1
    event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" lifecycle.teardown-requested hydra local \
        "{\"transcript_policy\":\"$(json_escape "$_lpt_policy")\"}" >/dev/null
}

lifecycle_finish_teardown() {
    _lft_branch="$1"
    _lft_session="$2"
    lifecycle_load_head "$_lft_branch" || return 0
    _lft_repo="$(sed -n '1p' "$(state_v2_project_dir "$LIFECYCLE_PROJECT_ID")/repo-root" 2>/dev/null || true)"
    lifecycle_write_head_scalar "$_lft_branch" desired-state stopped || return 1
    lifecycle_set_observed "$_lft_branch" exited hydra exact || return 1
    event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" lifecycle.torn-down hydra local '{}' >/dev/null || return 1
    run_hook post-teardown "" "$_lft_repo" "$_lft_session" "$_lft_branch"
}
