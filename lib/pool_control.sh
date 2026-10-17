#!/bin/sh
# Pool operator decisions and cancellation. Loaded with lib/pool.sh.
#
# Results are handed to callers through POOL_* variables.
# shellcheck disable=SC2034

# Usage: pool_resolve <pool-dir> <task> <succeeded|failed|retry> <by>
# An operator decision for a recovery-required task. `retry` consumes one of
# the pool's sealed repair rounds.
pool_resolve() {
    _pre_dir="$1" _pre_task="$2" _pre_outcome="$3" _pre_by="$4"
    _pre_td="$_pre_dir/tasks/$_pre_task"
    [ "$(pool_read "$_pre_td/state")" = recovery-required ] || {
        cli_error pool invalid_state "task '$_pre_task' is not recovery-required (state: $(pool_read "$_pre_td/state"))" "operator decisions apply to recovery-required tasks only"
        return 1
    }
    _pre_attempt="$_pre_td/attempt-$(pool_read "$_pre_td/attempts")"
    if [ "$_pre_outcome" = retry ]; then
        _pre_rounds="$(pool_read "$_pre_dir/repair-rounds")"
        [ "$((_pre_rounds + 1))" -le "$(pool_read "$_pre_dir/max-repair-rounds")" ] || {
            cli_error pool repair_exhausted "pool used its sealed maximum of $(pool_read "$_pre_dir/max-repair-rounds") repair round(s)" "decide succeeded or failed, or create a new pool"
            return 1
        }
        pool_write "$_pre_dir/repair-rounds" "$((_pre_rounds + 1))" || return 1
        pool_write "$_pre_attempt/decision" "retry by $_pre_by" || return 1
        pool_write "$_pre_td/state" ready || return 1
        pool_event "$_pre_dir" "$_pre_task" task.retrying "operator=$_pre_by repair_round=$((_pre_rounds + 1))"
    else
        pool_write "$_pre_attempt/outcome" "$_pre_outcome" || return 1
        pool_write "$_pre_attempt/source" operator || return 1
        pool_write "$_pre_attempt/decision" "$_pre_outcome by $_pre_by" || return 1
        pool_write "$_pre_attempt/completed-at" "$(pool_now)" || return 1
        pool_write "$_pre_td/state" "$_pre_outcome" || return 1
        pool_event "$_pre_dir" "$_pre_task" "task.$_pre_outcome" "operator=$_pre_by"
    fi
    POOL_TASK_STATE="$(pool_read "$_pre_td/state")"
}

# Cancel one task: queued work is cancelled now; claimed work is asked to stop
# (its recorded workflow run through `hydra workflow cancel`, its worker
# through the task's cancel marker) and ends when the owner reports or is
# reconciled. Sets POOL_CANCEL_RESULT to cancelled|signalled|terminal.
pool_cancel_task() {
    _pct_dir="$1" _pct_task="$2"
    _pct_td="$_pct_dir/tasks/$_pct_task"
    case "$(pool_read "$_pct_td/state")" in
        pending|ready)
            pool_write "$_pct_td/cancel-requested" "$(pool_now)" || return 1
            pool_write "$_pct_td/state" cancelled || return 1
            pool_event "$_pct_dir" "$_pct_task" task.cancelled request
            POOL_CANCEL_RESULT=cancelled
            ;;
        claimed)
            [ -f "$_pct_td/cancel-requested" ] || pool_write "$_pct_td/cancel-requested" "$(pool_now)" || return 1
            pool_event "$_pct_dir" "$_pct_task" task.cancel_requested "attempt=$(pool_read "$_pct_td/attempts")"
            if _pct_run="$(pool_task_run_id "$_pct_td" "$_pct_td/attempt-$(pool_read "$_pct_td/attempts")")"; then
                case "$(pool_read "$POOL_PROJECT_DIR/workflows/runs/$_pct_run/state")" in
                    running|waiting-approval|waiting-remote|queued)
                        (cd "$POOL_REPO_ROOT" && "$HYDRA_BIN_PATH" workflow cancel "$_pct_run") >/dev/null 2>&1 || true
                        ;;
                esac
            fi
            POOL_CANCEL_RESULT=signalled
            ;;
        recovery-required)
            pool_write "$_pct_td/state" cancelled || return 1
            pool_event "$_pct_dir" "$_pct_task" task.cancelled "operator unknown_outcome_abandoned"
            POOL_CANCEL_RESULT=cancelled
            ;;
        *) POOL_CANCEL_RESULT=terminal ;;
    esac
}

pool_cancel_all() {
    _pca_dir="$1"
    [ -f "$_pca_dir/cancel-requested" ] || pool_write "$_pca_dir/cancel-requested" "$(pool_now)" || return 1
    pool_write "$_pca_dir/state" cancelled || return 1
    pool_write "$_pca_dir/expansion-open" "" || return 1
    pool_event "$_pca_dir" "" pool.cancel_requested
    POOL_CANCELLED=0 POOL_SIGNALLED=0
    while IFS= read -r _pca_task; do
        [ -n "$_pca_task" ] || continue
        pool_cancel_task "$_pca_dir" "$_pca_task" || return 1
        case "$POOL_CANCEL_RESULT" in
            cancelled) POOL_CANCELLED=$((POOL_CANCELLED + 1)) ;;
            signalled) POOL_SIGNALLED=$((POOL_SIGNALLED + 1)) ;;
        esac
    done < "$_pca_dir/order"
}
