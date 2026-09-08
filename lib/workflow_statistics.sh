#!/bin/sh
# Read-only, bounded projection for the native statistics view. Versioned apart
# from the graph adapter: existing graph consumers retain their wire contract.
workflow_statistics_scalar() {
    if [ -f "$1" ] && [ ! -L "$1" ]; then
        _wst_value="$(dd if="$1" bs=256 count=1 2>/dev/null)"
        _wst_newline='
'
        _wst_value="${_wst_value%%"$_wst_newline"*}"
        case "$_wst_value" in *"$(printf '\t')"*|*"$(printf '\r')"*) _wst_value=- ;; esac
        [ "${#_wst_value}" -lt 256 ] || _wst_value=-
        [ -n "$_wst_value" ] && { printf '%s' "$_wst_value"; return; }
    fi
    printf '%s' '-'
}

workflow_statistics_data() (
    printf 'HYDRA_STATISTICS\t2\t%s\n' "$(date +%s)"
    _wst_root="$(workflow_runs_dir 2>/dev/null)" || { printf 'X\tProject identity unavailable\n'; printf 'Z\t0\t0\n'; return; }
    _wst_runs=0 _wst_steps=0
    for _wst_dir in "$_wst_root"/run_*; do
        [ -d "$_wst_dir" ] || continue
        if [ -L "$_wst_dir" ]; then printf 'X\tSkipped symlinked run\n'; continue; fi
        _wst_id="${_wst_dir##*/}"
        hydra_valid_id "$_wst_id" || { printf 'X\tSkipped invalid run ID\n'; continue; }
        if [ "$_wst_runs" -ge 128 ]; then printf 'X\tMore than 128 runs; partial sample in ID order\n'; break; fi
        _wst_runs=$((_wst_runs + 1))
        _wst_state="$(workflow_statistics_scalar "$_wst_dir/state")"
        if [ "$_wst_state" = running ] && ! workflow_run_owner_fresh "$_wst_dir"; then _wst_state=stale; fi
        _wst_complete=complete
        [ -f "$_wst_dir/graph.tsv" ] && [ ! -L "$_wst_dir/graph.tsv" ] || _wst_complete=partial
        workflow_statistics_run_metrics "$_wst_dir"
        printf 'R\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_wst_id" \
            "$(workflow_statistics_scalar "$_wst_dir/workflow-id")" "$_wst_state" \
            "$(workflow_statistics_scalar "$_wst_dir/project-id")" \
            "$(workflow_statistics_scalar "$_wst_dir/created-at")" "$_wst_complete" \
            "$_wsr_start" "$_wsr_end" "$_wsr_verified" "$_wsr_count" "$_wsr_plan"
        if [ "$_wst_complete" = partial ] || [ -L "$_wst_dir/steps" ]; then
            printf 'X\tRecorded steps unavailable\n'; continue
        fi
        _wst_per_run=0
        while IFS="$(printf '\t')" read -r _wst_tag _wst_step _wst_kind _wst_rest; do
            [ "$_wst_tag" = step ] || continue
            case "$_wst_step" in ''|*[!a-z0-9_-]*|[-_0-9]*) printf 'X\tSkipped invalid step ID\n'; continue ;; esac
            if [ "$_wst_per_run" -ge 128 ] || [ "$_wst_steps" -ge 1024 ]; then
                printf 'X\tStep limit reached; partial sample\n'; break
            fi
            _wst_per_run=$((_wst_per_run + 1)); _wst_steps=$((_wst_steps + 1))
            _wst_sd="$_wst_dir/steps/$_wst_step"
            if [ -L "$_wst_sd" ]; then printf 'X\tSkipped symlinked step\n'; _wst_steps=$((_wst_steps - 1)); continue; fi
            _wst_attempts="$(workflow_statistics_scalar "$_wst_sd/attempts")"
            _wst_status="$(workflow_statistics_scalar "$_wst_sd/state")"
            _wst_start="$(workflow_statistics_scalar "$_wst_sd/started-at")"
            _wst_end=-
            case "$_wst_attempts" in ''|*[!0-9]*) ;; *)
                if [ "${#_wst_attempts}" -le 6 ] && [ ! -L "$_wst_sd/attempt-$_wst_attempts" ]; then
                    _wst_end="$(workflow_statistics_scalar "$_wst_sd/attempt-$_wst_attempts/completed-at")"
                fi ;;
            esac
            # Scalar files are atomic individually, not a transaction. Discard
            # timing/count observations if a new attempt began during this read.
            if [ "$_wst_attempts" != "$(workflow_statistics_scalar "$_wst_sd/attempts")" ]; then
                _wst_attempts=- _wst_start=- _wst_end=-
            fi
            printf 'S\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_wst_id" "$_wst_step" \
                "$_wst_kind" "$_wst_status" "$_wst_attempts" "$_wst_start" "$_wst_end" \
                "$(workflow_statistics_scalar "$_wst_sd/initial-ready-at")" \
                "$(workflow_statistics_scalar "$_wst_sd/initial-started-at")"
        done < "$_wst_dir/graph.tsv"
    done
    printf 'Z\t%s\t%s\n' "$_wst_runs" "$_wst_steps"
)

# Called only after acquiring the run's drive lock. Old runs without a counter
# retain unknown recovery history. Approval continuation is not owner recovery.
workflow_statistics_begin() {
    _wsb_dir="$1"
    _wsb_state="$(workflow_statistics_scalar "$_wsb_dir/state")"
    if [ "$_wsb_state" = queued ] && [ ! -e "$_wsb_dir/started-at" ]; then
        workflow_atomic_scalar "$_wsb_dir/started-at" "$(date +%s)" || return 1
    elif [ "$_wsb_state" != waiting-approval ]; then
        _wsb_count="$(workflow_statistics_scalar "$_wsb_dir/recovery-count")"
        case "$_wsb_count" in ''|*[!0-9]*) return 0 ;; esac
        if [ "${#_wsb_count}" -gt 6 ] || [ "$_wsb_count" -ge 999999 ]; then
            workflow_atomic_scalar "$_wsb_dir/recovery-count" -
            return
        fi
        # expr treats leading-zero counters as decimal on POSIX shells.
        # shellcheck disable=SC2003
        _wsb_next="$(expr "$_wsb_count" + 1)" || return 1
        workflow_atomic_scalar "$_wsb_dir/recovery-count" "$_wsb_next" || return 1
    fi
}

# Snapshot run-level boundaries; verification is bound to the accepted plan.
workflow_statistics_run_metrics() {
    _wsr_dir="$1"
    _wsr_before="$(workflow_statistics_scalar "$_wsr_dir/state")"
    _wsr_count="$(workflow_statistics_scalar "$_wsr_dir/recovery-count")"
    _wsr_start="$(workflow_statistics_scalar "$_wsr_dir/started-at")"
    _wsr_end="$(workflow_statistics_scalar "$_wsr_dir/completed-at")"
    _wsr_verified=- _wsr_plan=0
    if [ -f "$_wsr_dir/compiled.json" ] && [ ! -L "$_wsr_dir/compiled.json" ]; then
        _wsr_plan=1
        _wsr_digest="$(workflow_statistics_scalar "$_wsr_dir/plan-accepted")"
        case "$_wsr_digest" in *[!a-f0-9]*|'') ;; *)
            if [ "${#_wsr_digest}" -eq 64 ] && [ "$_wsr_before" = succeeded ] &&
                [ "$_wsr_digest" = "$(workflow_statistics_scalar "$_wsr_dir/verification-plan-sha256")" ]; then
                _wsr_verified="$(workflow_statistics_scalar "$_wsr_dir/verified-at")"
            fi ;;
        esac
    fi
    if [ "$_wsr_before" != "$(workflow_statistics_scalar "$_wsr_dir/state")" ] ||
        [ "$_wsr_count" != "$(workflow_statistics_scalar "$_wsr_dir/recovery-count")" ]; then
        _wsr_start=- _wsr_end=- _wsr_verified=- _wsr_count=-
    fi
}
