#!/bin/sh
# Process orchestration only; native code owns remote identities and observations.
workflow_task_tool() (
    cmd_fleet_dispatch workflow-task "$@"
)
workflow_task_needs_owner() {
    [ "${HYDRA_WORKFLOW_LOCKED_RUN:-}" != "$1" ] && grep -q '^task_args' "$1/graph.tsv"
}
workflow_task_initialize() {
    grep -q '^task_args' "$1/graph.tsv" || return 0
    hydra_private_touch "$1/task-initialization.json" || return 1
    workflow_task_tool init "$1" "$(workflow_repo_root)" > "$1/task-initialization.json"
}
workflow_task_bindings_match() {
    grep -q '^task_args' "$1/graph.tsv" || return 0
    workflow_task_tool verify "$1" >/dev/null
}
# Usage: workflow_task_worker <run-dir> <step> <attempt>
# The worker of one task attempt, a process of its own started by the driver.
workflow_task_worker() {
    _wts_run="$1" _wts_id="$2" _wts_number="$3" _wts_sd="$1/steps/$2"
    _wts_attempt="$_wts_sd/attempt-$_wts_number"
    # Remote task records are Hydra evidence; no local user command runs here.
    umask 077
    _wts_pid=""
    trap '[ -z "$_wts_pid" ] || operations_signal_tree "$_wts_pid" TERM' HUP INT TERM
    if ! workflow_bindings_match "$_wts_run"; then
        workflow_atomic_scalar "$_wts_sd/state" recovery-required
        exit 1
    fi
    if [ ! -d "$_wts_attempt/remote" ]; then
        # No dispatch exists: incomplete preparation can be reconstructed.
        rm -rf "$_wts_attempt/inputs" "$_wts_attempt/outputs"
        if ! workflow_data_tool prepare "$_wts_run" "$_wts_id" "$_wts_attempt" > "$_wts_attempt/data-preparation.json"; then
            workflow_atomic_scalar "$_wts_sd/state" failed
            exit 1
        fi
    fi
    workflow_task_tool run "$_wts_run" "$_wts_id" "$_wts_attempt" > "$_wts_attempt/remote-response.json" 2> "$_wts_attempt/stderr" &
    _wts_pid=$!
    workflow_atomic_scalar "$_wts_sd/command-pid" "$_wts_pid"
    if wait "$_wts_pid"; then _wts_code=0; else _wts_code=$?; fi
    trap - HUP INT TERM
    # Reaped: its pid may be reused before the step records its result.
    rm -f "$_wts_sd/command-pid"
    if [ "$_wts_code" -eq 0 ]; then
        if [ -f "$_wts_attempt/outputs.json" ]; then
            workflow_data_tool verify-output "$_wts_run" "$_wts_id" "$_wts_attempt" > "$_wts_attempt/data-seal.json" || _wts_code=76
        else
            rm -rf "$_wts_attempt/artifacts"
            workflow_data_tool seal "$_wts_run" "$_wts_id" "$_wts_attempt" > "$_wts_attempt/data-seal.json" || _wts_code=76
        fi
    fi
    if [ "$_wts_code" -eq 0 ] && [ -f "$_wts_run/compiled.json" ]; then
        workflow_plan_tool step-check "$_wts_run" "$_wts_id" > "$_wts_attempt/validation-result.json" || _wts_code=1
    fi
    workflow_atomic_scalar "$_wts_attempt/exit-code" "$_wts_code"
    case "$_wts_code" in
        75|129|130|143) _wts_state=waiting-remote ;;
        76) _wts_state=recovery-required ;;
        0) _wts_state=succeeded ;;
        *) _wts_state=failed ;;
    esac
    if [ -f "$_wts_run/cancel-requested" ]; then
        case "$_wts_state" in succeeded|failed) _wts_state=cancelled ;; esac
    fi
    case "$_wts_state" in
        succeeded|failed|cancelled)
            workflow_atomic_scalar "$_wts_attempt/completed-at" "$(date +%s)" || exit 1
            ;;
    esac
    case "$_wts_state" in succeeded|failed) workflow_atomic_scalar "$_wts_sd/authoritative-attempt" "$_wts_number" ;; esac
    workflow_atomic_scalar "$_wts_sd/state" "$_wts_state"
    workflow_event "$_wts_run" "$_wts_id" "step.$_wts_state" task_observation
}
