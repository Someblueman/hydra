#!/bin/sh
# Sourced by tests/test_workflow_plan_task.sh. The fixture is removed with the
# CI job, so on any non-zero exit the case's EXIT trap calls this to put the
# run's evidence into the log. It only reads: it asserts nothing and a passing
# case never calls it. Every variable may still be unset (a failure before the
# run), so each is defaulted.
_wpe_dump() {
    # $1 label, $2 file: at most 20000 bytes, said when cut.
    printf -- '--- %s\n' "$1"
    if [ ! -f "$2" ]; then printf '(absent)\n'; return 0; fi
    head -c 20000 "$2"
    if [ "$(wc -c < "$2" | tr -d ' ')" -gt 20000 ]; then printf '\n[cut at 20000 bytes]\n'; fi
}
_wpe_names() {
    # $1 directory, $2 name prefix: the entry names, space separated.
    for _wpe_entry in "$1"/"$2"*; do
        [ -e "$_wpe_entry" ] || continue
        printf '%s ' "${_wpe_entry##*/}"
    done
}
workflow_plan_task_explain() {
    _wpe_status="${1:-?}"
    _wpe_fixture="${fixture:-}"
    if [ -z "$_wpe_fixture" ] || [ ! -d "$_wpe_fixture" ]; then return 0; fi
    _wpe_dir="${run_dir:-}"
    if [ -z "$_wpe_dir" ] || [ ! -d "$_wpe_dir" ]; then
        _wpe_dir="$(find "${HYDRA_HOME:-$_wpe_fixture/home}/state/v2/projects" -type d -path '*/workflows/runs/*' -prune -print 2>/dev/null | head -n 1)"
    fi
    {
        printf '=== plan task failure evidence (exit status %s) ===\n' "$_wpe_status"
        printf 'mode: repair=%s reuse=%s verdict=%s source=%s fault=%s effective=%s\n' \
            "${HYDRA_TEST_PLAN_REPAIR:-}" "${HYDRA_TEST_PLAN_REUSE:-0}" "${HYDRA_TEST_PLAN_TASK_VERDICT:-}" \
            "${HYDRA_TEST_PLAN_SOURCE:-0}" "${HYDRA_TEST_PLAN_REPAIR_FAULT:-0}" "${mode:-unset}"
        if [ -n "${explain_reason:-}" ]; then printf 'reason: %s\n' "$explain_reason"; fi
        printf 'plan run exit code: %s\n' "${code:-not-reached}"
        printf 'run id: %s\n' "${run:-not-reached}"
        printf 'run dir: %s\n' "${_wpe_dir:-none}"
        if [ -n "$_wpe_dir" ] && [ -d "$_wpe_dir" ]; then
            printf 'run state: %s\n' "$(cat "$_wpe_dir/state" 2>/dev/null || printf unreadable)"
            for _wpe_step in "$_wpe_dir"/steps/*; do
                [ -d "$_wpe_step" ] || continue
                printf 'step %s: state=%s attempts=%s authoritative=%s attempt-dirs=%s\n' "${_wpe_step##*/}" \
                    "$(cat "$_wpe_step/state" 2>/dev/null || printf '-')" "$(cat "$_wpe_step/attempts" 2>/dev/null || printf '-')" \
                    "$(cat "$_wpe_step/authoritative-attempt" 2>/dev/null || printf '-')" \
                    "$(_wpe_names "$_wpe_step" attempt-)"
            done
            printf 'repair-round: %s\n' "$(cat "$_wpe_dir/repair-round" 2>/dev/null || printf absent)"
            for _wpe_marker in repair-pending.json cancel-requested recovery-required; do
                if [ -e "$_wpe_dir/$_wpe_marker" ]; then printf 'present: %s\n' "$_wpe_marker"; else printf 'absent: %s\n' "$_wpe_marker"; fi
            done
            printf 'run dir entries: %s\n' "$(_wpe_names "$_wpe_dir" '')"
        fi
        printf 'accepted task records: %s\n' "$(find "${HYDRA_HOME:-$_wpe_fixture/home}/fleet/tasks" -name acceptance.json 2>/dev/null | wc -l | tr -d ' ')"
        _wpe_dump run.out "$_wpe_fixture/run.out"
        _wpe_dump run.err "$_wpe_fixture/run.err"
        _wpe_dump compile.json "$_wpe_fixture/compile.json"
        if [ -n "$_wpe_dir" ]; then _wpe_dump events.jsonl "$_wpe_dir/events.jsonl"; fi
        printf '=== end plan task failure evidence ===\n'
    } >&2 || :
}
