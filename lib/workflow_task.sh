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
    workflow_task_tool init "$1" "$(workflow_repo_root)" > "$1/task-initialization.json"
}
workflow_task_bindings_match() {
    grep -q '^task_args' "$1/graph.tsv" || return 0
    workflow_task_tool verify "$1" >/dev/null
}
workflow_task_resume() {
    while IFS="$(printf '\t')" read -r _wtr_tag _wtr_id _wtr_rest; do
        [ "$_wtr_tag" = task_args ] || continue
        _wtr_sd="$1/steps/$_wtr_id"
        rm -f "$_wtr_sd/cancel-reconciled"
        [ ! -f "$1/cancel-requested" ] || continue
        case "$(sed -n '1p' "$_wtr_sd/state")" in
            waiting-remote) workflow_atomic_scalar "$_wtr_sd/state" ready ;;
        esac
    done < "$1/graph.tsv"
}
workflow_task_start() {
    _wts_run="$1" _wts_id="$2" _wts_sd="$1/steps/$2"
    _wts_attempt="$_wts_sd/attempt-1"
    (umask 077; mkdir -p "$_wts_attempt") || return 1
    workflow_atomic_scalar "$_wts_sd/attempts" 1
    workflow_atomic_scalar "$_wts_sd/state" running
    workflow_event "$_wts_run" "$_wts_id" step.running task_reconciliation
    (
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
        case "$_wts_state" in succeeded|failed) workflow_atomic_scalar "$_wts_sd/authoritative-attempt" 1 ;; esac
        workflow_atomic_scalar "$_wts_sd/state" "$_wts_state"
        workflow_event "$_wts_run" "$_wts_id" "step.$_wts_state" task_observation
    ) &
    workflow_atomic_scalar "$_wts_sd/worker-pid" "$!"
}
workflow_waiting_state() {
    _wws_states="$(find "$1/steps" -name state -exec sed -n '1p' {} \;)"
    printf '%s\n' "$_wws_states" | grep -Eq '^waiting-(approval|remote)$' || return 1
    if printf '%s\n' "$_wws_states" | grep -Eq '^(ready|running|retrying)$'; then return 1; fi
    _wws_state=waiting-approval
    if printf '%s\n' "$_wws_states" | grep -q '^waiting-remote$'; then _wws_state=waiting-remote; fi
    workflow_atomic_scalar "$1/state" "$_wws_state"
    workflow_event "$1" "" "run.$(printf '%s' "$_wws_state" | tr '-' '_')"
}
workflow_task_cancel() {
    _wtc_id="$(basename "$2")"
    awk -F '\t' -v id="$_wtc_id" '$1=="task_args" && $2==id {found=1} END {exit !found}' "$1/graph.tsv" || return 1
    case "$3" in
        waiting-remote)
            if [ ! -f "$2/cancel-reconciled" ]; then
                workflow_atomic_scalar "$2/cancel-reconciled" 1
                workflow_task_start "$1" "$_wtc_id"
            fi
            ;;
        running) : ;; # The native observer sends the bound cancellation request.
        *) return 1 ;;
    esac
}
