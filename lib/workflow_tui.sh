#!/bin/sh
# Read-only projection of recorded workflow runs. No definition execution.
workflow_tui_data() (
    printf 'HYDRA_WORKFLOW_TUI\t1\n'
    _wtd_root="$(workflow_runs_dir 2>/dev/null)" || return 0
    [ -d "$_wtd_root" ] || return 0
    _wtd_runs=0
    _wtd_nodes=0
    for _wtd_dir in "$_wtd_root"/run_*; do
        [ -d "$_wtd_dir" ] || continue
        _wtd_id="$(basename "$_wtd_dir")"
        hydra_valid_id "$_wtd_id" || continue
        _wtd_runs=$((_wtd_runs + 1))
        if [ "$_wtd_runs" -gt 32 ]; then printf 'X\tMore than 32 runs; showing first 32 by ID\n'; break; fi
        _wtd_name="$(sed -n '1p' "$_wtd_dir/workflow-id" 2>/dev/null || printf unavailable)"
        _wtd_state="$(sed -n '1p' "$_wtd_dir/state" 2>/dev/null || printf unavailable)"
        if [ "$_wtd_state" = running ] && ! workflow_run_owner_fresh "$_wtd_dir"; then _wtd_state=stale; fi
        printf 'W\t%s\t%s\t%s\n' "$_wtd_id" "$_wtd_name" "$_wtd_state" | tr '\r' ' '
        if [ ! -f "$_wtd_dir/graph.tsv" ]; then printf 'X\tRecorded workflow graph unavailable\n'; continue; fi
        _wtd_steps=0
        while IFS="$(printf '\t')" read -r _wtd_tag _wtd_step _wtd_kind _wtd_needs _wtd_rest; do
            [ "$_wtd_tag" = step ] || continue
            # Validate before using a recorded ID as a path component.
            case "$_wtd_step" in ''|*[!a-z0-9_-]*|[-_0-9]*) printf 'X\tInvalid recorded step ID\n'; continue ;; esac
            _wtd_steps=$((_wtd_steps + 1)); _wtd_nodes=$((_wtd_nodes + 1))
            if [ "$_wtd_steps" -gt 128 ] || [ "$_wtd_nodes" -gt 512 ]; then
                printf 'X\tWorkflow exceeds visualization node limit\n'; return 0
            fi
            _wtd_status="$(sed -n '1p' "$_wtd_dir/steps/$_wtd_step/state" 2>/dev/null || printf unavailable)"
            _wtd_attempts="$(sed -n '1p' "$_wtd_dir/steps/$_wtd_step/attempts" 2>/dev/null || printf 0)"
            printf 'N\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_wtd_id" "$_wtd_step" "$_wtd_kind" "$_wtd_status" "$_wtd_attempts" "$_wtd_needs"
        done < "$_wtd_dir/graph.tsv"
    done
)
