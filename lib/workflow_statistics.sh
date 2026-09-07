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
    printf 'HYDRA_STATISTICS\t1\t%s\n' "$(date +%s)"
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
        printf 'R\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_wst_id" \
            "$(workflow_statistics_scalar "$_wst_dir/workflow-id")" "$_wst_state" \
            "$(workflow_statistics_scalar "$_wst_dir/project-id")" \
            "$(workflow_statistics_scalar "$_wst_dir/created-at")" "$_wst_complete"
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
            printf 'S\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_wst_id" "$_wst_step" \
                "$_wst_kind" "$_wst_status" "$_wst_attempts" "$_wst_start" "$_wst_end"
        done < "$_wst_dir/graph.tsv"
    done
    printf 'Z\t%s\t%s\n' "$_wst_runs" "$_wst_steps"
)
