#!/bin/sh
# Durable workflow scheduling, execution, cancellation, and recovery.

workflow_runs_dir() {
    _wrd_project="$(hydra_get_project_id)" || {
        cli_error workflow not_initialized "Hydra project identity is unavailable" "run hydra init"
        return 1
    }
    _wrd_project_dir="$(state_v2_project_dir "$_wrd_project")" || return 1
    printf '%s/workflows/runs\n' "$_wrd_project_dir"
}

workflow_atomic_scalar() {
    mkdir -p "$(dirname "$1")" || return 1
    state_v2_write_scalar "$1" "$2"
}

workflow_event() {
    _we_dir="$1" _we_step="$2" _we_type="$3" _we_detail="${4:-}"
    _we_file="$_we_dir/events.jsonl"
    _we_lock="$_we_dir/.events.lock"
    _we_tries=0
    while ! mkdir "$_we_lock" 2>/dev/null; do
        _we_lock_pid="$(sed -n '1p' "$_we_lock/owner-pid" 2>/dev/null || true)"
        if [ -n "$_we_lock_pid" ] && ! workflow_pid_alive "$_we_lock_pid"; then
            rm -rf "$_we_lock"
            continue
        fi
        _we_tries=$((_we_tries + 1))
        [ "$_we_tries" -lt 30 ] || return 1
        sleep 1
    done
    printf '%s\n' "$$" > "$_we_lock/owner-pid"
    _we_seq="$(awk 'END { print NR + 1 }' "$_we_file" 2>/dev/null || printf 1)"
    _we_now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '{"schema_version":1,"sequence":%s,"occurred_at":"%s","run_id":"%s","step_id":%s,"type":"%s","detail":"%s"}\n' \
        "$_we_seq" "$_we_now" "$(json_escape "$(sed -n '1p' "$_we_dir/run-id")")" \
        "$(if [ -n "$_we_step" ]; then printf '"%s"' "$(json_escape "$_we_step")"; else printf null; fi)" \
        "$(json_escape "$_we_type")" "$(json_escape "$_we_detail")" >> "$_we_file"
    _we_status=$?
    rm -rf "$_we_lock"
    return "$_we_status"
}

workflow_pid_alive() {
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    kill -0 "$1" 2>/dev/null
}

workflow_run_owner_active() {
    _wroa_dir="$1"
    _wroa_pid="$(sed -n '1p' "$_wroa_dir/owner-pid" 2>/dev/null || true)"
    workflow_pid_alive "$_wroa_pid"
}

workflow_run_owner_fresh() {
    _wrof_dir="$1"
    workflow_run_owner_active "$_wrof_dir" || return 1
    _wrof_heartbeat="$(sed -n '1p' "$_wrof_dir/heartbeat-at" 2>/dev/null || true)"
    case "$_wrof_heartbeat" in ''|*[!0-9]*) return 1 ;; esac
    [ "$(($(date +%s) - _wrof_heartbeat))" -le 15 ]
}

workflow_bindings_match() {
    _wbm_dir="$1"
    _wbm_base="$(sed -n '1p' "$_wbm_dir/base-commit")"
    [ "$(sed -n '1p' "$_wbm_dir/schema-version")" = 1 ] &&
    [ "$(sed -n '1p' "$_wbm_dir/project-id")" = "$(hydra_get_project_id)" ] &&
    git cat-file -e "$_wbm_base^{commit}" 2>/dev/null &&
    [ "$(git rev-parse HEAD 2>/dev/null || true)" = "$_wbm_base" ] &&
    [ "$(git hash-object "$_wbm_dir/resolved.yml")" = "$(sed -n '1p' "$_wbm_dir/definition-hash")" ] &&
    [ "$(workflow_parse "$_wbm_dir/resolved.yml" runtime | git hash-object --stdin)" = "$(git hash-object "$_wbm_dir/graph.tsv")" ] &&
    workflow_data_bindings_match "$_wbm_dir" &&
    workflow_plan_bindings_match "$_wbm_dir" &&
    workflow_task_bindings_match "$_wbm_dir"
}

# The profile handoff needs durable run/step bindings as well as execution options.
workflow_profile_command() {
    _wpc_dir="$1" _wpc_id="$2" _wpc_head="$3" _wpc_profile="$4" _wpc_timeout="$5"
    _wpc_profile_args="$(awk -F '\t' -v id="$_wpc_id" '$1=="profile_args" && $2==id {print $3 "\t" $4 "\t" $5 "\t" $6 "\t" $7}' "$_wpc_dir/graph.tsv")"
    _wpc_prompt_file="$(printf '%s\n' "$_wpc_profile_args" | cut -f1)"
    _wpc_prompt_input="$(printf '%s\n' "$_wpc_profile_args" | cut -f2)"
    _wpc_result_file="$(printf '%s\n' "$_wpc_profile_args" | cut -f3)"
    _wpc_requires="$(printf '%s\n' "$_wpc_profile_args" | cut -f4)"
    _wpc_resume_from="$(printf '%s\n' "$_wpc_profile_args" | cut -f5)"
    if [ "$_wpc_prompt_input" != - ]; then
        _wpc_prompt="$HYDRA_WORKFLOW_INPUTS_DIR/$_wpc_prompt_input"
    else
        _wpc_prompt="$("${HYDRA_BIN_CMD:-hydra}" path "$_wpc_head")/$_wpc_prompt_file"
    fi
    set -- exec --exit-code --json --branch "$_wpc_head" --profile "$_wpc_profile" --prompt-file "$_wpc_prompt"
    [ "$_wpc_result_file" = - ] || set -- "$@" --result-file "$HYDRA_WORKFLOW_OUTPUTS_DIR/$_wpc_result_file"
    [ "$_wpc_requires" = - ] || set -- "$@" --require "$_wpc_requires"
    if [ "$_wpc_resume_from" != - ]; then
        _wpc_previous="$(sed -n '1p' "$_wpc_dir/steps/$_wpc_resume_from/authoritative-attempt")"
        case "$_wpc_previous" in ''|*[!0-9]*) return 2 ;; esac
        _wpc_resume_id="$(cmd_fleet_dispatch agent-profile run-id "$_wpc_dir/steps/$_wpc_resume_from/attempt-$_wpc_previous/stdout")" || return 2
        set -- "$@" --resume-run "$_wpc_resume_id"
    fi
    [ -z "$_wpc_timeout" ] || set -- "$@" --timeout "$_wpc_timeout"
    "$HYDRA_BIN_PATH" "$@"
}

workflow_step_command() {
    _wsc_dir="$1" _wsc_id="$2"; shift 2
    _wsc_kind="$1"; shift
    _wsc_head="$1" _wsc_branch="$2" _wsc_group="$3" _wsc_profile="$4" _wsc_command="$5"
    _wsc_message="$6" _wsc_name="$7" _wsc_by="$8" _wsc_reason="$9"; shift 9
    _wsc_policy="$1" _wsc_timeout="$2" _wsc_force="$3" _wsc_allow="$4" _wsc_argv="$5"
    [ "$_wsc_head" != - ] || _wsc_head=""; [ "$_wsc_branch" != - ] || _wsc_branch=""
    [ "$_wsc_group" != - ] || _wsc_group=""; [ "$_wsc_profile" != - ] || _wsc_profile=""
    [ "$_wsc_command" != - ] || _wsc_command=""; [ "$_wsc_message" != - ] || _wsc_message=""
    [ "$_wsc_name" != - ] || _wsc_name=""; [ "$_wsc_by" != - ] || _wsc_by=""
    [ "$_wsc_reason" != - ] || _wsc_reason=""; [ "$_wsc_policy" != - ] || _wsc_policy=""
    [ "$_wsc_timeout" != - ] || _wsc_timeout=""; [ "$_wsc_force" != - ] || _wsc_force=""
    [ "$_wsc_allow" != - ] || _wsc_allow=""; [ "$_wsc_argv" != - ] || _wsc_argv=""
    case "$_wsc_kind" in
        spawn)
            set -- spawn "$_wsc_branch"
            [ -z "$_wsc_group" ] || set -- "$@" --group "$_wsc_group"
            if [ -n "$_wsc_profile" ]; then set -- "$@" --profile "$_wsc_profile"; else set -- "$@" --no-agent; fi
            [ -z "$_wsc_policy" ] || set -- "$@" --completion-policy "$_wsc_policy"
            ;;
        wait) set -- wait "$_wsc_head"; [ -z "$_wsc_timeout" ] || set -- "$@" --timeout "$_wsc_timeout" ;;
        message) set -- send "$_wsc_head" "$_wsc_message" ;;
        approve) set -- gate approve "$_wsc_head" --name "$_wsc_name" --by "$_wsc_by"; [ -z "$_wsc_reason" ] || set -- "$@" --reason "$_wsc_reason" ;;
        kill) set -- kill "$_wsc_head"; [ "$_wsc_force" != true ] || set -- "$@" --force ;;
        exec)
            if [ -n "$_wsc_profile" ]; then
                workflow_profile_command "$_wsc_dir" "$_wsc_id" "$_wsc_head" "$_wsc_profile" "$_wsc_timeout"
                return $?
            elif [ -n "$_wsc_argv" ]; then
                set -- exec --exit-code
                [ -z "$_wsc_head" ] || set -- "$@" --branch "$_wsc_head"
                [ -z "$_wsc_timeout" ] || set -- "$@" --timeout "$_wsc_timeout"
                set -- "$@" --
                _wsc_oldifs="$IFS"
                _wsc_oldflags="$-"
                IFS=,
                set -f
                for _wsc_arg in $_wsc_argv; do set -- "$@" "$_wsc_arg"; done
                IFS="$_wsc_oldifs"
                case "$_wsc_oldflags" in *f*) ;; *) set +f ;; esac
            else
                set -- exec --exit-code
                [ -z "$_wsc_head" ] || set -- "$@" --branch "$_wsc_head"
                [ -z "$_wsc_timeout" ] || set -- "$@" --timeout "$_wsc_timeout"
                set -- "$@" --shell "$_wsc_command" --allow-shell
            fi
            ;;
        gate)
            set -- gate run "$_wsc_head" --name "$_wsc_name" --
            if [ -n "$_wsc_argv" ]; then
                _wsc_oldifs="$IFS"
                _wsc_oldflags="$-"
                IFS=,
                set -f
                for _wsc_arg in $_wsc_argv; do set -- "$@" "$_wsc_arg"; done
                IFS="$_wsc_oldifs"
                case "$_wsc_oldflags" in *f*) ;; *) set +f ;; esac
            else
                [ "$_wsc_allow" = true ] || return 2
                set -- "$@" sh -c "$_wsc_command"
            fi
            ;;
        *) return 2 ;;
    esac
    "$HYDRA_BIN_PATH" "$@"
}

workflow_refresh_states() {
    _wrs_dir="$1"
    _wrs_changed=1
    while [ "$_wrs_changed" -eq 1 ]; do
        _wrs_changed=0
        while IFS="$(printf '\t')" read -r _wrs_tag _wrs_id _wrs_kind _wrs_needs _wrs_retry _wrs_idem _wrs_rest; do
            [ "$_wrs_tag" = step ] || continue
            _wrs_sd="$_wrs_dir/steps/$_wrs_id"
            _wrs_state="$(sed -n '1p' "$_wrs_sd/state")"
            [ "$_wrs_state" = queued ] || continue
            _wrs_ready=1
            _wrs_failed=0
            _wrs_oldifs="$IFS"
            IFS=,
            for _wrs_dep in $_wrs_needs; do
                [ "$_wrs_dep" != - ] || continue
                _wrs_ds="$(sed -n '1p' "$_wrs_dir/steps/$_wrs_dep/state")"
                case "$_wrs_ds" in
                    succeeded) ;;
                    failed|cancelled|recovery-required) _wrs_failed=1 ;;
                    *) _wrs_ready=0 ;;
                esac
            done
            IFS="$_wrs_oldifs"
            if [ "$_wrs_failed" -eq 1 ]; then
                workflow_atomic_scalar "$_wrs_sd/state" cancelled
                workflow_event "$_wrs_dir" "$_wrs_id" step.cancelled dependency_failed
                _wrs_changed=1
            elif [ "$_wrs_ready" -eq 1 ]; then
                if [ "$(sed -n '1p' "$_wrs_sd/attempts")" = 0 ]; then
                    workflow_atomic_scalar "$_wrs_sd/initial-ready-at" "$(date +%s)"
                fi
                workflow_atomic_scalar "$_wrs_sd/state" ready
                workflow_event "$_wrs_dir" "$_wrs_id" step.ready
                _wrs_changed=1
            fi
        done < "$_wrs_dir/graph.tsv"
    done
}

workflow_disk_available() {
    _wda_dir="$1"
    _wda_required="$(sed -n '1p' "$_wda_dir/disk-mb")"
    _wda_root="$(workflow_repo_root)" || return 1
    _wda_kb="$(df -Pk "$_wda_root" 2>/dev/null | awk 'END { print $4 }')"
    case "$_wda_kb" in ''|*[!0-9]*) return 1 ;; esac
    [ "$_wda_kb" -ge $((_wda_required * 1024)) ] || {
        workflow_event "$_wda_dir" "" run.resource_refused "disk_mb=$_wda_required"
        return 1
    }
}

workflow_recover_running_steps() {
    _wrr_dir="$1"
    while IFS="$(printf '\t')" read -r _wrr_tag _wrr_id _wrr_kind _wrr_needs _wrr_retry _wrr_idem _wrr_rest; do
        [ "$_wrr_tag" = step ] || continue
        _wrr_sd="$_wrr_dir/steps/$_wrr_id"
        [ "$(sed -n '1p' "$_wrr_sd/state")" = running ] || continue
        _wrr_pid="$(sed -n '1p' "$_wrr_sd/worker-pid" 2>/dev/null || true)"
        workflow_pid_alive "$_wrr_pid" && continue
        _wrr_attempt="$(sed -n '1p' "$_wrr_sd/attempts")"
        _wrr_exit="$(sed -n '1p' "$_wrr_sd/attempt-$_wrr_attempt/exit-code" 2>/dev/null || true)"
        if [ "$_wrr_kind" = task ]; then
            workflow_atomic_scalar "$_wrr_sd/state" waiting-remote
            continue
        fi
        workflow_attempt_result "$_wrr_dir" "$_wrr_id" "$_wrr_retry" "$_wrr_idem" "${_wrr_exit:-unknown}"
    done < "$_wrr_dir/graph.tsv"
}

workflow_start_step() {
    _wss_dir="$1"
    _wss_id="$2"
    _wss_line="$(awk -F '\t' -v id="$_wss_id" '$1=="step" && $2==id {print; exit}' "$_wss_dir/graph.tsv")"
    _wss_oldifs="$IFS"
    _wss_oldflags="$-"
    IFS="$(printf '\t')"
    set -f
    # The runtime TSV uses explicit '-' placeholders for every empty field.
    # shellcheck disable=SC2086
    set -- $_wss_line
    IFS="$_wss_oldifs"
    case "$_wss_oldflags" in *f*) ;; *) set +f ;; esac
    shift
    _wss_id="$1"
    _wss_kind="$2"
    _wss_needs="$3"
    _wss_retry="$4"
    _wss_idem="$5"
    shift 5
    if [ "$_wss_kind" = approval-wait ]; then
        workflow_approval_create "$_wss_dir" "$_wss_id" "$1" "$7" "$6" "${11}"
        return $?
    fi
    if [ "$_wss_kind" = task ]; then
        workflow_task_start "$_wss_dir" "$_wss_id"
        return $?
    fi
    _wss_sd="$_wss_dir/steps/$_wss_id"
    _wss_attempt="$(sed -n '1p' "$_wss_sd/attempts")"
    _wss_attempt=$((_wss_attempt + 1))
    _wss_attempt_dir="$_wss_sd/attempt-$_wss_attempt"
    (umask 077; mkdir -p "$_wss_attempt_dir") || return 1
    workflow_atomic_scalar "$_wss_sd/attempts" "$_wss_attempt"
    workflow_atomic_scalar "$_wss_sd/state" running
    _wss_started="$(date +%s)"
    workflow_atomic_scalar "$_wss_sd/started-at" "$_wss_started"
    if [ "$_wss_attempt" -eq 1 ]; then
        workflow_atomic_scalar "$_wss_sd/initial-started-at" "$_wss_started"
    fi
    if [ "$_wss_kind" = approve ]; then
        workflow_atomic_scalar "$_wss_attempt_dir/decision-source" workflow-policy || return 1
    fi
    workflow_event "$_wss_dir" "$_wss_id" step.running "attempt=$_wss_attempt"
    (
        _ws_command_pid=""
        _ws_cancelled=0
        trap '_ws_cancelled=1; [ -z "$_ws_command_pid" ] || operations_signal_tree "$_ws_command_pid" TERM' HUP INT TERM
        if ! workflow_plan_bindings_match "$_wss_dir"; then
            workflow_atomic_scalar "$_wss_sd/state" recovery-required
            workflow_event "$_wss_dir" "$_wss_id" step.recovery_required stale_plan
            exit 1
        fi
        if ! workflow_approval_guard "$_wss_dir" "$_wss_id"; then
            workflow_atomic_scalar "$_wss_sd/state" recovery-required
            workflow_event "$_wss_dir" "$_wss_id" step.recovery_required stale_approval
            exit 1
        fi
        if [ -f "$_wss_dir/data.json" ]; then
            if ! workflow_data_definition_matches "$_wss_dir" ||
                ! workflow_data_tool prepare "$_wss_dir" "$_wss_id" "$_wss_attempt_dir" > "$_wss_attempt_dir/data-preparation.json"; then
                workflow_atomic_scalar "$_wss_attempt_dir/exit-code" 1
                workflow_atomic_scalar "$_wss_sd/state" failed
                workflow_event "$_wss_dir" "$_wss_id" step.failed invalid_inputs
                exit 1
            fi
            HYDRA_WORKFLOW_INPUTS_DIR="$_wss_attempt_dir/inputs"
            HYDRA_WORKFLOW_OUTPUTS_DIR="$_wss_attempt_dir/outputs"
            export HYDRA_WORKFLOW_INPUTS_DIR HYDRA_WORKFLOW_OUTPUTS_DIR
        fi
        if [ -f "$_wss_dir/compiled.json" ]; then
            HYDRA_WORKFLOW_VALIDATION_FILE="$_wss_attempt_dir/validation-context.json"
            if ! workflow_plan_tool check-context "$_wss_dir/compiled.json" "$_wss_id" > "$HYDRA_WORKFLOW_VALIDATION_FILE"; then
                workflow_atomic_scalar "$_wss_attempt_dir/exit-code" 1
                workflow_atomic_scalar "$_wss_sd/state" failed
                workflow_event "$_wss_dir" "$_wss_id" step.failed invalid_validation_context
                exit 1
            fi
            export HYDRA_WORKFLOW_VALIDATION_FILE
        else
            unset HYDRA_WORKFLOW_VALIDATION_FILE
        fi
        workflow_step_command "$_wss_dir" "$_wss_id" "$_wss_kind" "$@" >"$_wss_attempt_dir/stdout" 2>"$_wss_attempt_dir/stderr" &
        _ws_command_pid=$!
        workflow_atomic_scalar "$_wss_sd/command-pid" "$_ws_command_pid"
        if wait "$_ws_command_pid"; then _ws_code=0; else _ws_code=$?; fi
        trap - HUP INT TERM
        [ "$_ws_cancelled" -eq 0 ] || _ws_code=143
        if [ "$_ws_code" -eq 0 ] && [ -f "$_wss_dir/data.json" ]; then
            if ! workflow_data_tool seal "$_wss_dir" "$_wss_id" "$_wss_attempt_dir" > "$_wss_attempt_dir/data-seal.json"; then
                _ws_code=1
                workflow_event "$_wss_dir" "$_wss_id" step.invalid_outputs
            fi
        fi
        workflow_atomic_scalar "$_wss_attempt_dir/exit-code" "$_ws_code"
        workflow_atomic_scalar "$_wss_attempt_dir/completed-at" "$(date +%s)"
        if [ -f "$_wss_dir/cancel-requested" ]; then
            workflow_atomic_scalar "$_wss_sd/state" cancelled
            workflow_event "$_wss_dir" "$_wss_id" step.cancelled "attempt=$_wss_attempt"
        else
            workflow_attempt_result "$_wss_dir" "$_wss_id" "$_wss_retry" "$_wss_idem" "$_ws_code"
        fi
    ) &
    workflow_atomic_scalar "$_wss_sd/worker-pid" "$!"
}

workflow_cancel_steps() {
    _wcs_dir="$1"
    : > "$_wcs_dir/residual-children.tsv"
    while IFS= read -r _wcs_sd; do
        [ -n "$_wcs_sd" ] || continue
        _wcs_state="$(sed -n '1p' "$_wcs_sd/state")"
        workflow_task_cancel "$_wcs_dir" "$_wcs_sd" "$_wcs_state" && continue
        case "$_wcs_state" in
            queued|ready|retrying|waiting-approval)
                workflow_atomic_scalar "$_wcs_sd/state" cancelled
                workflow_event "$_wcs_dir" "$(basename "$_wcs_sd")" step.cancelled request
                ;;
            running)
                _wcs_worker="$(sed -n '1p' "$_wcs_sd/worker-pid" 2>/dev/null || true)"
                _wcs_command="$(sed -n '1p' "$_wcs_sd/command-pid" 2>/dev/null || true)"
                if workflow_pid_alive "$_wcs_command"; then
                    operations_signal_tree "$_wcs_command" TERM
                fi
                _wcs_worker_state="$(ps -o stat= -p "$_wcs_worker" 2>/dev/null | sed -n '1p' | tr -d ' ')"
                case "$_wcs_worker_state" in Z*) wait "$_wcs_worker" 2>/dev/null || true ;; esac
                if workflow_pid_alive "$_wcs_worker"; then
                    printf '%s\t%s\t%s\n' "$(basename "$_wcs_sd")" "$_wcs_worker" "$_wcs_command" >> "$_wcs_dir/residual-children.tsv"
                fi
                ;;
        esac
    done <<EOF
$(find "$_wcs_dir/steps" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | LC_ALL=C sort)
EOF
}

workflow_drive() {
    _wd_dir="$1"
    if workflow_task_needs_owner "$_wd_dir"; then workflow_task_tool drive "$_wd_dir"; return $?; fi
    _wd_drive_lock="$_wd_dir/.drive.lock"
    if ! mkdir "$_wd_drive_lock" 2>/dev/null; then
        _wd_existing="$(sed -n '1p' "$_wd_dir/owner-pid" 2>/dev/null || true)"
        workflow_run_owner_active "$_wd_dir" && {
            cli_error workflow already_running "workflow owner is still alive" "wait or cancel the run"
            return 1
        }
        rm -rf "$_wd_drive_lock"
        mkdir "$_wd_drive_lock" || return 1
    fi
    _load_lib workflow_statistics
    workflow_statistics_begin "$_wd_dir" || { rm -rf "$_wd_drive_lock"; return 1; }
    workflow_atomic_scalar "$_wd_dir/owner-pid" "$$" || return 1
    workflow_atomic_scalar "$_wd_dir/heartbeat-at" "$(date +%s)" || return 1
    workflow_atomic_scalar "$_wd_dir/state" running || return 1
    workflow_event "$_wd_dir" "" run.running
    if [ ! -f "$_wd_dir/cancel-requested" ]; then
        workflow_approval_resume "$_wd_dir" || {
            workflow_atomic_scalar "$_wd_dir/state" recovery-required
            rm -rf "$_wd_drive_lock"
            return 1
        }
    fi
    workflow_plan_repair "$_wd_dir" repair-resume >/dev/null || return 1
    workflow_recover_running_steps "$_wd_dir" || return 1
    workflow_task_resume "$_wd_dir" || return 1
    trap 'workflow_atomic_scalar "$_wd_dir/cancel-requested" "$(date +%s)"' HUP INT TERM
    _wd_parallelism="$(sed -n '1p' "$_wd_dir/parallelism")"
    while :; do
        workflow_atomic_scalar "$_wd_dir/heartbeat-at" "$(date +%s)"
        if workflow_plan_expired "$_wd_dir" && [ ! -f "$_wd_dir/cancel-requested" ]; then
            workflow_atomic_scalar "$_wd_dir/cancel-requested" "$(date +%s)"
            workflow_event "$_wd_dir" "" run.budget_exhausted plan_deadline
        fi
        workflow_recover_running_steps "$_wd_dir"
        if [ -f "$_wd_dir/cancel-requested" ]; then
            [ -f "$_wd_dir/cancel-started-at" ] || workflow_atomic_scalar "$_wd_dir/cancel-started-at" "$(date +%s)"
            workflow_cancel_steps "$_wd_dir"
        else
            workflow_retry_ready "$_wd_dir"
            workflow_refresh_states "$_wd_dir"
        fi

        _wd_active="$(find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -Ec '^running$' || true)"
        if [ ! -f "$_wd_dir/cancel-requested" ]; then
            _wd_slots=$((_wd_parallelism - _wd_active))
            while [ "$_wd_slots" -gt 0 ]; do
                _wd_next="$(workflow_next_step "$_wd_dir")" || {
                    workflow_atomic_scalar "$_wd_dir/state" recovery-required
                    rm -rf "$_wd_drive_lock"
                    return 1
                }
                [ -n "$_wd_next" ] || break
                _wd_next_kind="$(awk -F '\t' -v id="$_wd_next" '$1=="step" && $2==id { print $3; exit }' "$_wd_dir/graph.tsv")"
                if [ "$_wd_next_kind" = spawn ]; then
                    _wd_spawn_active="$(awk -F '\t' '$1=="step" && $3=="spawn" { print $2 }' "$_wd_dir/graph.tsv" | while IFS= read -r _wd_spawn_id; do
                        if [ "$(sed -n '1p' "$_wd_dir/steps/$_wd_spawn_id/state")" = running ]; then
                            printf 'running\n'
                        fi
                    done | grep -Ec '^running$' || true)"
                    [ "$_wd_spawn_active" -eq 0 ] || break
                fi
                if ! workflow_disk_available "$_wd_dir"; then
                    workflow_atomic_scalar "$_wd_dir/steps/$_wd_next/state" failed
                    workflow_event "$_wd_dir" "$_wd_next" step.failed disk_safeguard
                    break
                fi
                workflow_start_step "$_wd_dir" "$_wd_next" || {
                    workflow_atomic_scalar "$_wd_dir/steps/$_wd_next/state" failed
                    workflow_event "$_wd_dir" "$_wd_next" step.failed launch_error
                    break
                }
                _wd_slots=$((_wd_slots - 1))
                _wd_active=$((_wd_active + 1))
            done
        fi

        if workflow_waiting_state "$_wd_dir"; then
            trap - HUP INT TERM
            rm -rf "$_wd_drive_lock"
            return 3
        fi

        _wd_nonterminal="$(find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -Ec '^(queued|ready|running|retrying)$' || true)"
        if [ "$_wd_nonterminal" -eq 0 ]; then
            _wd_repaired="$(workflow_plan_repair "$_wd_dir")" || return 1
            if [ "$_wd_repaired" = 1 ]; then
                workflow_event "$_wd_dir" "" run.repair new_candidate_round
                continue
            fi
            # A worker can finish after the cancellation snapshot was taken.
            # Refresh it before publishing a terminal run state.
            [ ! -f "$_wd_dir/cancel-requested" ] || workflow_cancel_steps "$_wd_dir"
            if find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -q '^recovery-required$'; then
                _wd_final=recovery-required
            elif find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -q '^failed$'; then
                _wd_final=failed
            elif [ -f "$_wd_dir/cancel-requested" ]; then
                _wd_final=cancelled
            else
                _wd_final=succeeded
            fi
            if [ "$_wd_final" = succeeded ] && ! workflow_plan_finish "$_wd_dir"; then
                _wd_final=failed
                workflow_event "$_wd_dir" "" run.delivery_rejected missing_or_negative_verification
            fi
            case "$_wd_final" in succeeded|failed|cancelled)
                workflow_atomic_scalar "$_wd_dir/completed-at" "$(date +%s)" ;;
            esac
            workflow_atomic_scalar "$_wd_dir/state" "$_wd_final"
            workflow_event "$_wd_dir" "" "run.$_wd_final"
            trap - HUP INT TERM
            rm -rf "$_wd_drive_lock"
            [ "$_wd_final" = succeeded ]
            return
        fi

        if [ -f "$_wd_dir/cancel-requested" ] && [ -s "$_wd_dir/residual-children.tsv" ]; then
            _wd_cancel_at="$(sed -n '1p' "$_wd_dir/cancel-started-at")"
            if [ "$(($(date +%s) - _wd_cancel_at))" -ge 5 ]; then
                workflow_atomic_scalar "$_wd_dir/state" recovery-required
                workflow_event "$_wd_dir" "" run.recovery_required residual_children
                trap - HUP INT TERM
                rm -rf "$_wd_drive_lock"
                return 1
            fi
        fi
        sleep 1
    done
}
