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
    hydra_private_mkdir "$(dirname "$1")" || return 1
    state_v2_write_scalar "$1" "$2"
}

# The event lock is the directory <run>/.events.lock, taken with
# hydra_exclusive_mkdir; its owner-pid names the process that writes. The
# native driver (take_event_lock in src/fleet/workflow/workflow_drive_io.c)
# takes and breaks it by the same rules.

# True when a live process other than this one has announced, in
# <lock>.breaking.<pid>, that it is breaking the lock. An announcement counts
# only while its process lives; a dead one is left alone, as its PID may name
# a new process that has just announced under the same name.
# Usage: workflow_event_lock_breaker <lock> <own-announcement>
workflow_event_lock_breaker() {
    for _welk_claim in "$1".breaking.*; do
        [ "$_welk_claim" != "$2" ] || continue
        [ -f "$_welk_claim" ] || continue
        if workflow_pid_alive "${_welk_claim##*.breaking.}"; then return 0; fi
    done
    return 1
}

# True while the lock is the one a waiting writer judged stale: it still names
# the dead owner the writer read or, for a lock without an owner record, has
# not been modified since the writer made its watch file. A lock taken in the
# meantime names a live owner or is a newer directory.
# Usage: workflow_event_lock_stale <lock> <owner-pid|''> <watch-file>
workflow_event_lock_stale() {
    [ -d "$1" ] || return 1
    [ "$(sed -n '1p' "$1/owner-pid" 2>/dev/null || true)" = "$2" ] || return 1
    if [ -n "$2" ]; then
        if workflow_pid_alive "$2"; then return 1; fi
        return 0
    fi
    [ -f "$3" ] && [ -z "$(find "$1" -prune -newer "$3" 2>/dev/null)" ]
}

# Breaks a lock whose owner died, or that was abandoned without an owner
# record. Checking the owner and then deleting the directory is not enough: of
# two waiting writers that both saw the stale lock, the slower would delete
# the lock the faster had taken in between, and both would append an event
# with the same sequence number. So, as remove_stale_lock_dir (lib/locks.sh):
#   1. Announce: create <lock>.breaking.<pid of this process> beside the lock.
#   2. Give way to any other live announcement. Of two breakers at least one
#      sees the other, because each announces before it looks; when both give
#      way, both try again.
#   3. Check again that the lock is the stale one (workflow_event_lock_stale).
#      No other breaker is past step 2, so a lock that is stale now stays this
#      one until step 4.
#   4. Rename the directory away, which frees the name in one step, and delete
#      it under its new name.
# Usage: workflow_event_lock_break <lock> <owner-pid|''> <watch-file> <own-pid>
# Returns: 0 when this process removed the lock, 1 otherwise
workflow_event_lock_break() {
    _welb_claim="$1.breaking.$4"
    (umask 077; : > "$_welb_claim") 2>/dev/null || return 1
    _welb_status=1
    if ! workflow_event_lock_breaker "$1" "$_welb_claim" && workflow_event_lock_stale "$1" "$2" "$3"; then
        _welb_grave="$1.dead.$4"
        rm -rf "$_welb_grave"
        if mv "$1" "$_welb_grave" 2>/dev/null; then _welb_status=0; fi
        rm -rf "$_welb_grave"
    fi
    rm -f "$_welb_claim"
    return "$_welb_status"
}

workflow_event() {
    _we_dir="$1" _we_step="$2" _we_type="$3" _we_detail="${4:-}"
    _we_file="$_we_dir/events.jsonl"
    _we_lock="$_we_dir/.events.lock"
    _we_tries=0
    hydra_private_touch "$_we_file" || return 1
    # The lock names the process that writes, which recovery below tests for
    # life. In a subshell $$ would name the top-level process instead: a
    # writer whose parent exited would lose its live lock to recovery, and a
    # dead writer's lock would be kept while that parent lived. It is known
    # before the lock is taken, so the lock is without an owner record only
    # for as long as writing the record takes.
    _we_pid="$(exec sh -c 'printf %s "$PPID"')"
    _we_ownerless=0
    _we_watch=""
    while ! (umask 077; hydra_exclusive_mkdir "$_we_lock") 2>/dev/null; do
        _we_lock_pid="$(sed -n '1p' "$_we_lock/owner-pid" 2>/dev/null || true)"
        if [ -n "$_we_lock_pid" ]; then
            _we_ownerless=0
            # The owner died holding the lock: break it and try again at once.
            if ! workflow_pid_alive "$_we_lock_pid" &&
                workflow_event_lock_break "$_we_lock" "$_we_lock_pid" "" "$_we_pid"; then
                continue
            fi
        elif [ -d "$_we_lock" ]; then
            # No owner record: the writer is between creating the lock and
            # recording itself, or it died there and nothing will ever release
            # the lock. It is abandoned once it has stayed unmodified, still
            # without a record, over five polls after it was first seen; the
            # watch file made at that first sight is what "unmodified since"
            # is measured against.
            if [ "$_we_ownerless" -gt 0 ] && [ -n "$(find "$_we_lock" -prune -newer "$_we_watch" 2>/dev/null)" ]; then
                _we_ownerless=0
            fi
            if [ "$_we_ownerless" -eq 0 ]; then
                _we_watch="$_we_lock.watch.$_we_pid"
                rm -f "$_we_watch"
                (umask 077; : > "$_we_watch") 2>/dev/null || true
            fi
            _we_ownerless=$((_we_ownerless + 1))
            if [ "$_we_ownerless" -gt 5 ] && workflow_event_lock_break "$_we_lock" "" "$_we_watch" "$_we_pid"; then
                _we_ownerless=0
                continue
            fi
        fi
        _we_tries=$((_we_tries + 1))
        if [ "$_we_tries" -ge 30 ]; then
            [ -z "$_we_watch" ] || rm -f "$_we_watch"
            return 1
        fi
        # A held lock is waited for a second at a time; one released since
        # the failed try is tried again at once.
        [ ! -d "$_we_lock" ] || sleep 1
    done
    [ -z "$_we_watch" ] || rm -f "$_we_watch"
    (umask 077; printf '%s\n' "$_we_pid" > "$_we_lock/owner-pid")
    _we_seq="$(awk 'END { print NR + 1 }' "$_we_file" 2>/dev/null || printf 1)"
    _we_now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '{"schema_version":1,"sequence":%s,"occurred_at":"%s","run_id":"%s","step_id":%s,"type":"%s","detail":"%s"}\n' \
        "$_we_seq" "$_we_now" "$(json_escape "$(sed -n '1p' "$_we_dir/run-id")")" \
        "$(if [ -n "$_we_step" ]; then printf '"%s"' "$(json_escape "$_we_step")"; else printf null; fi)" \
        "$(json_escape "$_we_type")" "$(json_escape "$_we_detail")" >> "$_we_file"
    _we_status=$?
    rm -rf "$_we_lock"
    # Runs launched from an agent proposal notify that planning conversation.
    if [ "$_we_status" -eq 0 ] && [ -f "$_we_dir/planning-head" ]; then
        if { command -v workflow_plan_notify >/dev/null 2>&1 || _load_lib workflow_plan_notify; }; then
            workflow_plan_notify "$_we_dir" "$_we_step" "$_we_type" || true
        fi
    fi
    return "$_we_status"
}

workflow_pid_alive() {
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    kill -0 "$1" 2>/dev/null || return 1
    # A killed worker whose parent died stays a zombie until init reaps it,
    # which some container inits do late or never; it still answers kill -0
    # but runs nothing, so recovery must not wait on it. An unreadable state
    # keeps the process counted as alive.
    case "$(ps -o stat= -p "$1" 2>/dev/null)" in *Z*) return 1 ;; esac
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
    # A compiled plan guards what it copied at admission instead of the checkout.
    { [ -f "$_wbm_dir/compiled.json" ] || [ "$(git rev-parse HEAD 2>/dev/null || true)" = "$_wbm_base" ]; } &&
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
            _wsc_terminal="$(awk -F '\t' -v id="$_wsc_id" '$1=="terminal_mode" && $2==id {print $3}' "$_wsc_dir/graph.tsv")"
            [ "$_wsc_terminal" != headless ] || set -- "$@" --headless
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
                for _wsc_arg in $_wsc_argv; do
                    # A compiled plan passes one of this step's materialized
                    # inputs (such as a proposal asset) as @input/<name>.
                    case "$_wsc_arg" in
                        @input/?*)
                            if [ -f "$_wsc_dir/compiled.json" ] && [ -n "${HYDRA_WORKFLOW_INPUTS_DIR:-}" ]; then
                                _wsc_arg="$HYDRA_WORKFLOW_INPUTS_DIR/${_wsc_arg#@input/}"
                            fi
                            ;;
                    esac
                    set -- "$@" "$_wsc_arg"
                done
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

# Usage: workflow_start_step <run-dir> <step> [<attempt>]
# The shell half of a native drive (workflow_driver_hook): creates an approval
# request, or, given the attempt number the driver recorded, runs that
# attempt's worker in the foreground. The driver records each start itself.
workflow_start_step() {
    _wss_dir="$1"
    _wss_id="$2"
    _wss_worker="${3:-}"
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
    # A worker is started only by the driver, which names its attempt.
    [ -n "$_wss_worker" ] || return 2
    if [ "$_wss_kind" = task ]; then
        workflow_task_worker "$_wss_dir" "$_wss_id" "$_wss_worker"
        return
    fi
    workflow_step_run "$_wss_dir" "$_wss_id" "$_wss_kind" "$_wss_retry" "$_wss_idem" "$_wss_worker" "$@"
}

# Usage: workflow_step_run <run-dir> <step> <kind> <retry> <idempotent> <attempt> <argument>...
# The worker of one step attempt: runs the step command and records its
# result. It runs in a subshell or a process of its own, and may exit.
workflow_step_run() {
    _wss_dir="$1" _wss_id="$2" _wss_kind="$3" _wss_retry="$4" _wss_idem="$5" _wss_attempt="$6"
    shift 6
    _wss_sd="$_wss_dir/steps/$_wss_id"
    _wss_attempt_dir="$_wss_sd/attempt-$_wss_attempt"
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
        hydra_private_touch "$_wss_attempt_dir/data-preparation.json" || exit 1
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
        hydra_private_touch "$HYDRA_WORKFLOW_VALIDATION_FILE" || exit 1
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
    # An exec step on one of a plan's own heads: commands record the commit
    # they start on, agent steps are committed when they finish.
    _ws_hook_kind="" _ws_hook_head="" _ws_tree=""
    IFS="$(printf '\t')" read -r _ws_hook_kind _ws_hook_head _ws_hook_rest <<EOF
$(workflow_plan_step_hook "$_wss_dir" "$_wss_id")
EOF
    if [ -n "$_ws_hook_kind" ]; then
        if ! _ws_tree="$(workflow_plan_worktree "$_wss_dir" "$_ws_hook_head")" ||
            { [ "$_ws_hook_kind" != agent ] && ! workflow_plan_record_commit "$_ws_tree" "$_wss_attempt_dir/head-commit"; }; then
            workflow_atomic_scalar "$_wss_attempt_dir/exit-code" 1
            workflow_atomic_scalar "$_wss_sd/state" failed
            workflow_event "$_wss_dir" "$_wss_id" step.failed worker_head_unavailable
            exit 1
        fi
    fi
    # Attempt evidence is private; the step command keeps the user's umask.
    # Transcripts exist only once the command starts.
    hydra_private_touch "$_wss_attempt_dir/stdout" "$_wss_attempt_dir/stderr" || exit 1
    workflow_step_command "$_wss_dir" "$_wss_id" "$_wss_kind" "$@" >"$_wss_attempt_dir/stdout" 2>"$_wss_attempt_dir/stderr" &
    _ws_command_pid=$!
    workflow_atomic_scalar "$_wss_sd/command-pid" "$_ws_command_pid"
    if wait "$_ws_command_pid"; then _ws_code=0; else _ws_code=$?; fi
    trap - HUP INT TERM
    # The command is reaped, so its pid is free for the system to reuse while
    # the step is still running (committing, sealing). A cancel must not
    # signal whatever process gets it next.
    rm -f "$_wss_sd/command-pid"
    [ "$_ws_cancelled" -eq 0 ] || _ws_code=143
    if [ "$_ws_code" -eq 0 ] && [ -n "$_ws_tree" ]; then
        if [ "$_ws_hook_kind" = agent ]; then
            if ! workflow_plan_commit "$_wss_dir" "$_wss_id" "$_ws_tree" "$_wss_attempt_dir" \
                >>"$_wss_attempt_dir/stderr" 2>&1; then
                _ws_code=1
                workflow_event "$_wss_dir" "$_wss_id" step.commit_failed
            fi
        elif ! workflow_plan_record_commit "$_ws_tree" "$_wss_attempt_dir/head-commit-after"; then
            _ws_code=1
        elif [ "$_ws_hook_kind" = command ] &&
            ! workflow_plan_tool command-report "$_wss_dir" "$_wss_id" "$_wss_attempt_dir" >/dev/null; then
            # Hydra writes a command check's report from the exit status
            # and the commit the command ran on.
            _ws_code=1
            workflow_event "$_wss_dir" "$_wss_id" step.check_unbound
        fi
    fi
    # Outputs the step command wrote with the user's umask become run
    # evidence; seal them privately like every other run record.
    hydra_private_tree "$_wss_attempt_dir/outputs" || true
    if [ "$_ws_code" -eq 0 ] && [ -f "$_wss_dir/data.json" ]; then
        if ! hydra_private_touch "$_wss_attempt_dir/data-seal.json" ||
            ! workflow_data_tool seal "$_wss_dir" "$_wss_id" "$_wss_attempt_dir" > "$_wss_attempt_dir/data-seal.json"; then
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
}

# Usage: workflow_native_driver
# Prints the hydra-fleet that drives runs. Every run is driven by
# `hydra-fleet workflow-drive`; there is no shell driver. Without a hydra-fleet
# that answers the capability probe (none installed, or one built before the
# native driver or for another run layout), or with HYDRA_NATIVE_DRIVER=0, which
# once selected the removed shell driver, it prints one error and returns 1.
# No other value of HYDRA_NATIVE_DRIVER has an effect.
workflow_native_driver() {
    _wnd_next="build it with make build-fleet (it needs JSON-C development files and pkg-config) and install it with ./install.sh, which puts it at <prefix>/libexec/hydra/hydra-fleet, or point HYDRA_FLEET_BIN at one"
    if [ "${HYDRA_NATIVE_DRIVER:-}" = 0 ]; then
        cli_error workflow helper_missing "workflows need the hydra-fleet helper: the shell workflow driver was removed, so HYDRA_NATIVE_DRIVER=0 no longer selects it, and nothing was started" "unset HYDRA_NATIVE_DRIVER; $_wnd_next"
        return 1
    fi
    if ! _wnd_binary="$(fleet_binary 2>/dev/null)"; then
        cli_error workflow helper_missing "workflows need the hydra-fleet helper, which was not found: workflows are unavailable and nothing was started" "$_wnd_next"
        return 1
    fi
    if [ "$("$_wnd_binary" workflow-drive --capability 2>/dev/null)" != 'workflow-drive 1' ]; then
        cli_error workflow helper_unsupported "workflows need the hydra-fleet helper, and $_wnd_binary is too old or for another run layout (it does not answer workflow-drive 1): workflows are unavailable and nothing was started" "$_wnd_next"
        return 1
    fi
    printf '%s\n' "$_wnd_binary"
}

# Usage: workflow_drive_native <run-dir> <hydra-fleet> [tail]
# Hands a run whose drive lock this shell holds to the native driver. When the
# drive is the last thing this CLI process does ("tail") and no subshell runs
# it, the process becomes the driver (its PID is the run's owner-pid);
# otherwise the driver is a child that outlives this shell if it dies.
workflow_drive_native() {
    # The driver's shell workers keep this shell's errexit setting.
    HYDRA_DRIVER_SHELL_FLAGS="$-"
    export HYDRA_DRIVER_SHELL_FLAGS
    if [ "${3:-}" = tail ] && [ "$$" = "$(exec sh -c 'printf %s "$PPID"')" ]; then
        exec "$2" workflow-drive "$1"
    fi
    # A terminal's interrupt reaches the driver too, which records the
    # cancellation; this shell waits for the driver to finish it.
    trap ':' HUP INT TERM
    if "$2" workflow-drive "$1"; then _wdn_status=0; else _wdn_status=$?; fi
    trap - HUP INT TERM
    return "$_wdn_status"
}

# Usage: workflow_driver_hook <operation> <run-dir> [<argument>...]
# The parts of a drive that the native driver runs as shell processes.
workflow_driver_hook() {
    _wdh_op="$1" _wdh_dir="$2"
    shift 2
    case "${HYDRA_DRIVER_SHELL_FLAGS:-e}" in *e*) ;; *) set +e ;; esac
    case "$_wdh_op:$#" in
        step-worker:2) workflow_start_step "$_wdh_dir" "$1" "$2" ;;
        approval-create:1) workflow_start_step "$_wdh_dir" "$1" ;;
        approval-resume:0) workflow_approval_resume "$_wdh_dir" ;;
        repair:1) workflow_plan_repair "$_wdh_dir" "$1" ;;
        finish:0) workflow_plan_finish "$_wdh_dir" ;;
        retire:0) _load_lib workflow_retire; workflow_retire_verifiers "$_wdh_dir" ;;
        notify:2) _load_lib workflow_plan_notify; workflow_plan_notify "$_wdh_dir" "$1" "$2" ;;
        *) return 2 ;;
    esac
}

# Usage: workflow_drive <run-dir> [tail]
# Returns 1 with one error, and changes nothing, when no hydra-fleet can drive
# the run (see workflow_native_driver).
workflow_drive() {
    _wd_dir="$1"
    if workflow_task_needs_owner "$_wd_dir"; then workflow_task_tool drive "$_wd_dir"; return $?; fi
    _wd_native="$(workflow_native_driver)" || return 1
    _wd_drive_lock="$_wd_dir/.drive.lock"
    if ! (umask 077; hydra_exclusive_mkdir "$_wd_drive_lock") 2>/dev/null; then
        workflow_run_owner_active "$_wd_dir" && {
            cli_error workflow already_running "workflow owner is still alive" "wait or cancel the run"
            return 1
        }
        rm -rf "$_wd_drive_lock"
        (umask 077; hydra_exclusive_mkdir "$_wd_drive_lock") || return 1
    fi
    _load_lib workflow_statistics
    workflow_statistics_begin "$_wd_dir" || true
    workflow_drive_native "$_wd_dir" "$_wd_native" "${2:-}"
}
