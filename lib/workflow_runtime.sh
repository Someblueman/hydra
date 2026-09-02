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

workflow_bindings_match() {
    _wbm_dir="$1"
    [ "$(sed -n '1p' "$_wbm_dir/schema-version")" = 1 ] &&
    [ "$(sed -n '1p' "$_wbm_dir/project-id")" = "$(hydra_get_project_id)" ] &&
    git cat-file -e "$(sed -n '1p' "$_wbm_dir/base-commit")^{commit}" 2>/dev/null &&
    [ "$(git hash-object "$_wbm_dir/resolved.yml")" = "$(sed -n '1p' "$_wbm_dir/definition-hash")" ]
}

workflow_step_command() {
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
            if [ -n "$_wsc_argv" ]; then
                set -- exec
                [ -z "$_wsc_head" ] || set -- "$@" --branch "$_wsc_head"
                [ -z "$_wsc_timeout" ] || set -- "$@" --timeout "$_wsc_timeout"
                set -- "$@" --
                _wsc_oldifs="$IFS"
                IFS=,
                for _wsc_arg in $_wsc_argv; do set -- "$@" "$_wsc_arg"; done
                IFS="$_wsc_oldifs"
            else
                set -- exec
                [ -z "$_wsc_head" ] || set -- "$@" --branch "$_wsc_head"
                [ -z "$_wsc_timeout" ] || set -- "$@" --timeout "$_wsc_timeout"
                set -- "$@" --shell "$_wsc_command" --allow-shell
            fi
            ;;
        gate)
            set -- gate run "$_wsc_head" --name "$_wsc_name" --
            if [ -n "$_wsc_argv" ]; then
                _wsc_oldifs="$IFS"
                IFS=,
                for _wsc_arg in $_wsc_argv; do set -- "$@" "$_wsc_arg"; done
                IFS="$_wsc_oldifs"
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
        if [ -n "$_wrr_exit" ]; then
            if [ "$_wrr_exit" -eq 0 ]; then
                workflow_atomic_scalar "$_wrr_sd/state" succeeded
                workflow_atomic_scalar "$_wrr_sd/authoritative-attempt" "$_wrr_attempt"
            elif [ "$_wrr_idem" = true ] && [ "$_wrr_attempt" -le "$_wrr_retry" ]; then
                workflow_atomic_scalar "$_wrr_sd/state" ready
            else
                workflow_atomic_scalar "$_wrr_sd/state" failed
                workflow_atomic_scalar "$_wrr_sd/authoritative-attempt" "$_wrr_attempt"
            fi
        elif [ "$_wrr_idem" = true ] && [ "$_wrr_attempt" -le "$_wrr_retry" ]; then
            workflow_atomic_scalar "$_wrr_sd/state" ready
            workflow_event "$_wrr_dir" "$_wrr_id" step.recovered "interrupted_attempt=$_wrr_attempt"
        else
            workflow_atomic_scalar "$_wrr_sd/state" recovery-required
            workflow_event "$_wrr_dir" "$_wrr_id" step.recovery_required "uncertain_attempt=$_wrr_attempt"
        fi
    done < "$_wrr_dir/graph.tsv"
}

workflow_start_step() {
    _wss_dir="$1"
    _wss_id="$2"
    _wss_line="$(awk -F '\t' -v id="$_wss_id" '$1=="step" && $2==id {print; exit}' "$_wss_dir/graph.tsv")"
    _wss_oldifs="$IFS"
    IFS="$(printf '\t')"
    # The runtime TSV uses explicit '-' placeholders for every empty field.
    # shellcheck disable=SC2086
    set -- $_wss_line
    IFS="$_wss_oldifs"
    shift
    _wss_id="$1"
    _wss_kind="$2"
    _wss_needs="$3"
    _wss_retry="$4"
    _wss_idem="$5"
    shift 5
    _wss_sd="$_wss_dir/steps/$_wss_id"
    _wss_attempt="$(sed -n '1p' "$_wss_sd/attempts")"
    _wss_attempt=$((_wss_attempt + 1))
    _wss_attempt_dir="$_wss_sd/attempt-$_wss_attempt"
    mkdir -p "$_wss_attempt_dir" || return 1
    workflow_atomic_scalar "$_wss_sd/attempts" "$_wss_attempt"
    workflow_atomic_scalar "$_wss_sd/state" running
    workflow_atomic_scalar "$_wss_sd/started-at" "$(date +%s)"
    workflow_event "$_wss_dir" "$_wss_id" step.running "attempt=$_wss_attempt"
    (
        _ws_command_pid=""
        _ws_cancelled=0
        trap '_ws_cancelled=1; [ -z "$_ws_command_pid" ] || kill -TERM "$_ws_command_pid" 2>/dev/null || true' HUP INT TERM
        workflow_step_command "$_wss_kind" "$@" >"$_wss_attempt_dir/stdout" 2>"$_wss_attempt_dir/stderr" &
        _ws_command_pid=$!
        workflow_atomic_scalar "$_wss_sd/command-pid" "$_ws_command_pid"
        if wait "$_ws_command_pid"; then _ws_code=0; else _ws_code=$?; fi
        trap - HUP INT TERM
        workflow_atomic_scalar "$_wss_attempt_dir/exit-code" "$_ws_code"
        workflow_atomic_scalar "$_wss_attempt_dir/completed-at" "$(date +%s)"
        if [ -f "$_wss_dir/cancel-requested" ]; then
            workflow_atomic_scalar "$_wss_sd/state" cancelled
            workflow_event "$_wss_dir" "$_wss_id" step.cancelled "attempt=$_wss_attempt"
        elif [ "$_ws_cancelled" -eq 1 ]; then
            if [ "$_wss_idem" = true ] && [ "$_wss_attempt" -le "$_wss_retry" ]; then
                workflow_atomic_scalar "$_wss_sd/state" ready
                workflow_event "$_wss_dir" "$_wss_id" step.recovered "interrupted_attempt=$_wss_attempt"
            else
                workflow_atomic_scalar "$_wss_sd/state" recovery-required
                workflow_event "$_wss_dir" "$_wss_id" step.recovery_required "uncertain_attempt=$_wss_attempt"
            fi
        elif [ "$_ws_code" -eq 0 ]; then
            workflow_atomic_scalar "$_wss_sd/authoritative-attempt" "$_wss_attempt"
            workflow_atomic_scalar "$_wss_sd/state" succeeded
            workflow_event "$_wss_dir" "$_wss_id" step.succeeded "attempt=$_wss_attempt"
        elif [ "$_wss_attempt" -le "$_wss_retry" ] && [ "$_wss_idem" = true ]; then
            workflow_atomic_scalar "$_wss_sd/state" retrying
            workflow_event "$_wss_dir" "$_wss_id" step.retrying "attempt=$_wss_attempt"
            workflow_atomic_scalar "$_wss_sd/state" ready
        else
            workflow_atomic_scalar "$_wss_sd/authoritative-attempt" "$_wss_attempt"
            workflow_atomic_scalar "$_wss_sd/state" failed
            workflow_event "$_wss_dir" "$_wss_id" step.failed "attempt=$_wss_attempt exit=$_ws_code"
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
        case "$_wcs_state" in
            queued|ready|retrying)
                workflow_atomic_scalar "$_wcs_sd/state" cancelled
                workflow_event "$_wcs_dir" "$(basename "$_wcs_sd")" step.cancelled request
                ;;
            running)
                _wcs_worker="$(sed -n '1p' "$_wcs_sd/worker-pid" 2>/dev/null || true)"
                _wcs_command="$(sed -n '1p' "$_wcs_sd/command-pid" 2>/dev/null || true)"
                workflow_pid_alive "$_wcs_command" && kill -TERM "$_wcs_command" 2>/dev/null || true
                workflow_pid_alive "$_wcs_worker" && kill -TERM "$_wcs_worker" 2>/dev/null || true
                workflow_pid_alive "$_wcs_worker" && printf '%s\t%s\t%s\n' "$(basename "$_wcs_sd")" "$_wcs_worker" "$_wcs_command" >> "$_wcs_dir/residual-children.tsv"
                ;;
        esac
    done <<EOF
$(find "$_wcs_dir/steps" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | LC_ALL=C sort)
EOF
}

workflow_drive() {
    _wd_dir="$1"
    _wd_drive_lock="$_wd_dir/.drive.lock"
    if ! mkdir "$_wd_drive_lock" 2>/dev/null; then
        _wd_existing="$(sed -n '1p' "$_wd_dir/owner-pid" 2>/dev/null || true)"
        workflow_pid_alive "$_wd_existing" && {
            cli_error workflow already_running "workflow owner is still alive" "wait or cancel the run"
            return 1
        }
        rm -rf "$_wd_drive_lock"
        mkdir "$_wd_drive_lock" || return 1
    fi
    workflow_atomic_scalar "$_wd_dir/owner-pid" "$$" || return 1
    workflow_atomic_scalar "$_wd_dir/heartbeat-at" "$(date +%s)" || return 1
    workflow_atomic_scalar "$_wd_dir/state" running || return 1
    workflow_event "$_wd_dir" "" run.running
    trap 'workflow_atomic_scalar "$_wd_dir/cancel-requested" "$(date +%s)"' HUP INT TERM
    _wd_parallelism="$(sed -n '1p' "$_wd_dir/parallelism")"
    while :; do
        workflow_atomic_scalar "$_wd_dir/heartbeat-at" "$(date +%s)"
        workflow_recover_running_steps "$_wd_dir"
        if [ -f "$_wd_dir/cancel-requested" ]; then
            [ -f "$_wd_dir/cancel-started-at" ] || workflow_atomic_scalar "$_wd_dir/cancel-started-at" "$(date +%s)"
            workflow_cancel_steps "$_wd_dir"
        else
            workflow_refresh_states "$_wd_dir"
        fi

        _wd_active="$(find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -Ec '^running$' || true)"
        if [ ! -f "$_wd_dir/cancel-requested" ]; then
            _wd_slots=$((_wd_parallelism - _wd_active))
            while [ "$_wd_slots" -gt 0 ]; do
                _wd_next="$(awk -F '\t' '$1=="step" {print $2}' "$_wd_dir/graph.tsv" | while IFS= read -r _wd_id; do [ "$(sed -n '1p' "$_wd_dir/steps/$_wd_id/state")" = ready ] && { printf '%s\n' "$_wd_id"; break; }; done || true)"
                [ -n "$_wd_next" ] || break
                _wd_next_kind="$(awk -F '\t' -v id="$_wd_next" '$1=="step" && $2==id { print $3; exit }' "$_wd_dir/graph.tsv")"
                if [ "$_wd_next_kind" = spawn ]; then
                    _wd_spawn_active="$(awk -F '\t' '$1=="step" && $3=="spawn" { print $2 }' "$_wd_dir/graph.tsv" | while IFS= read -r _wd_spawn_id; do [ "$(sed -n '1p' "$_wd_dir/steps/$_wd_spawn_id/state")" = running ] && printf 'running\n'; done | grep -Ec '^running$' || true)"
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

        _wd_nonterminal="$(find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -Ec '^(queued|ready|running|retrying)$' || true)"
        if [ "$_wd_nonterminal" -eq 0 ]; then
            if find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -q '^recovery-required$'; then
                _wd_final=recovery-required
            elif find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -q '^failed$'; then
                _wd_final=failed
            elif [ -f "$_wd_dir/cancel-requested" ]; then
                _wd_final=cancelled
            else
                _wd_final=succeeded
            fi
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

