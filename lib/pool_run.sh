#!/bin/sh
# Pool worker loop: claim, execute through the public commands, report.
#
# This is not a scheduler. Each claimed task is handed to `hydra workflow run`
# or `hydra exec`, which own admission, supervision, retries and recovery.
# The loop only renews its lease, forwards cancellation and reports the exit.
# Results are handed to callers through POOL_RUN_* variables.
# shellcheck disable=SC2034

# Build the public argv for a task. Sets POOL_EXEC_ARGS via "$@" emulation:
# the caller sources the result with `eval "set -- $POOL_EXEC_ARGS"`.
pool_run_command() {
    _prc_td="$1"
    _prc_args=""
    case "$(pool_read "$_prc_td/kind")" in
        workflow)
            set -- workflow run "$(pool_read "$_prc_td/definition")"
            ;;
        exec)
            set -- exec --exit-code --branch "$(pool_read "$_prc_td/definition")" --
            while IFS= read -r _prc_arg || [ -n "$_prc_arg" ]; do
                set -- "$@" "$_prc_arg"
            done < "$_prc_td/argv"
            ;;
        *) return 1 ;;
    esac
    for _prc_arg in "$@"; do
        _prc_quoted="$(printf '%s' "$_prc_arg" | sed "s/'/'\\\\''/g")"
        _prc_args="$_prc_args '$_prc_quoted'"
    done
    POOL_EXEC_ARGS="$_prc_args"
}

# Usage: pool_run_execute <pool-name> <pool-dir> <task> <token> <lease>
# Runs the task's public command in the repository, captures its output as
# attempt evidence and sets POOL_RUN_OUTCOME (succeeded|failed) and
# POOL_RUN_ID (run_... or -).
pool_run_execute() {
    _pre_name="$1" _pre_dir="$2" _pre_task="$3" _pre_token="$4" _pre_lease="$5"
    _pre_td="$_pre_dir/tasks/$_pre_task"
    _pre_attempt="$_pre_td/attempt-$(pool_read "$_pre_td/attempts")"
    POOL_RUN_OUTCOME=failed POOL_RUN_ID=-
    pool_run_command "$_pre_td" || return 1
    hydra_private_touch "$_pre_attempt/stdout" "$_pre_attempt/stderr" || return 1
    rm -f "$_pre_attempt/exit-code"
    eval "set -- $POOL_EXEC_ARGS"
    (
        # The CLI runs under set -e: capture every exit status explicitly so
        # the exit-code record is always written.
        if cd "$POOL_REPO_ROOT" && "$HYDRA_BIN_PATH" "$@" > "$_pre_attempt/stdout" 2> "$_pre_attempt/stderr"; then
            _pre_code=0
        else
            _pre_code=$?
        fi
        state_v2_write_scalar "$_pre_attempt/exit-code" "$_pre_code"
    ) &
    _pre_child=$!
    pool_write "$_pre_attempt/command-pid" "$_pre_child" || true
    _pre_signalled=0
    _pre_renew_every=$((_pre_lease / 2))
    [ "$_pre_renew_every" -ge 1 ] || _pre_renew_every=1
    _pre_last_renew="$(pool_now)"
    while [ ! -f "$_pre_attempt/exit-code" ]; do
        sleep 1
        if [ "$_pre_signalled" -eq 0 ]; then
            if [ "$POOL_RUN_STOP" -eq 1 ] || [ -f "$_pre_td/cancel-requested" ] || [ -f "$_pre_dir/cancel-requested" ]; then
                operations_signal_tree "$_pre_child" TERM
                _pre_signalled=1
            elif [ "$(($(pool_now) - _pre_last_renew))" -ge "$_pre_renew_every" ]; then
                _pre_last_renew="$(pool_now)"
                if pool_lock "$_pre_name" >/dev/null 2>&1; then
                    if ! pool_renew "$_pre_dir" "$_pre_task" "$_pre_token" "$_pre_lease" >/dev/null 2>&1; then
                        # The claim is no longer ours: stop working on it.
                        operations_signal_tree "$_pre_child" TERM
                        _pre_signalled=1
                    fi
                    pool_unlock "$_pre_name"
                fi
            fi
        fi
    done
    wait "$_pre_child" 2>/dev/null || true
    if [ "$(pool_read "$_pre_td/kind")" = workflow ] && _pre_run="$(pool_task_run_id "$_pre_td" "$_pre_attempt")"; then
        POOL_RUN_ID="$_pre_run"
        pool_write "$_pre_attempt/run-id" "$_pre_run" || true
    fi
    [ "$(pool_read "$_pre_attempt/exit-code")" = 0 ] && POOL_RUN_OUTCOME=succeeded
    return 0
}

# Usage: pool_run_worker <pool-name> <worker> <max-tasks|0> <lease> <json>
# Sets POOL_RUN_COUNT, POOL_RUN_LAST_REASON and POOL_RUN_RESULTS (JSON array).
pool_run_worker() {
    _prw_name="$1" _prw_worker="$2" _prw_max="$3" _prw_lease="$4" _prw_json="$5"
    _prw_dir="$(pool_dir "$_prw_name")" || return 1
    POOL_RUN_COUNT=0 POOL_RUN_LAST_REASON="" POOL_RUN_RESULTS="" POOL_RUN_STOP=0
    trap 'POOL_RUN_STOP=1' HUP INT TERM
    while :; do
        if [ "$POOL_RUN_STOP" -eq 1 ]; then POOL_RUN_LAST_REASON=interrupted; break; fi
        pool_lock "$_prw_name" || return 1
        if ! pool_claim "$_prw_dir" "$_prw_worker" "$$" "$_prw_lease"; then
            pool_unlock "$_prw_name"
            return 1
        fi
        pool_unlock "$_prw_name"
        if [ -z "$POOL_CLAIM_TASK" ]; then POOL_RUN_LAST_REASON="$POOL_CLAIM_REASON"; break; fi
        _prw_task="$POOL_CLAIM_TASK" _prw_token="$POOL_CLAIM_TOKEN"
        [ "$_prw_json" -eq 1 ] || printf 'Task %s: claimed by %s\n' "$_prw_task" "$_prw_worker"
        pool_run_execute "$_prw_name" "$_prw_dir" "$_prw_task" "$_prw_token" "$_prw_lease" || {
            POOL_RUN_OUTCOME=failed POOL_RUN_ID=-
        }
        pool_lock "$_prw_name" || return 1
        if pool_complete "$_prw_dir" "$_prw_task" "$_prw_token" "$POOL_RUN_OUTCOME" "$POOL_RUN_ID"; then
            _prw_state="$POOL_TASK_STATE"
        else
            _prw_state=stale-owner
        fi
        pool_unlock "$_prw_name"
        [ "$_prw_json" -eq 1 ] || printf 'Task %s: %s (reported %s)\n' "$_prw_task" "$_prw_state" "$POOL_RUN_OUTCOME"
        POOL_RUN_RESULTS="$POOL_RUN_RESULTS${POOL_RUN_RESULTS:+,}{\"task_id\":\"$(json_escape "$_prw_task")\",\"outcome\":\"$POOL_RUN_OUTCOME\",\"state\":\"$_prw_state\",\"run_id\":$(json_string_or_null "$([ "$POOL_RUN_ID" = - ] || printf '%s' "$POOL_RUN_ID")")}"
        POOL_RUN_COUNT=$((POOL_RUN_COUNT + 1))
        if [ "$_prw_max" -gt 0 ] && [ "$POOL_RUN_COUNT" -ge "$_prw_max" ]; then POOL_RUN_LAST_REASON=max_tasks; break; fi
    done
    trap - HUP INT TERM
    return 0
}
