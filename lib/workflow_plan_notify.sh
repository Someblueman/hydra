#!/bin/sh
# Planning-conversation notices for runs launched from an agent proposal.
# A notice is an inbox record for the exact planning head instance recorded at
# launch. Recording one never claims the agent read it, and a failure here
# never changes the run.

# Usage: workflow_plan_notice_text <run-id> <type> <step>
# Prints the notice for events that need the user, or nothing.
workflow_plan_notice_text() {
    case "$2" in
        run.created)
            printf 'Hydra: run %s started from your approved plan. Follow it with hydra workflow status %s --json; relay progress, approval requests (hydra workflow requests %s), failures and the final result to the user. Do not execute or modify the run yourself.' "$1" "$1" "$1" ;;
        approval.requested)
            printf 'Hydra: run %s is waiting for an approval decision on step %s. Tell the user; the user decides in Hydra (hydra workflow requests %s).' "$1" "$3" "$1" ;;
        step.failed|step.recovery-required|step.recovery_required)
            printf 'Hydra: step %s %s in run %s. Tell the user and show hydra workflow status %s; do not retry or modify the run yourself.' "$3" "$(workflow_plan_notice_state "$2")" "$1" "$1" ;;
        run.succeeded|run.failed|run.cancelled|run.recovery-required|run.recovery_required)
            printf 'Hydra: run %s finished: %s. Report the outcome to the user (hydra workflow plan result %s).' "$1" "$(workflow_plan_notice_state "$2")" "$1" ;;
    esac
}

workflow_plan_notice_state() {
    case "$1" in
        *recovery*) printf 'needs recovery' ;;
        *) printf '%s' "${1#*.}" ;;
    esac
}

# Usage: workflow_plan_notify <run-dir> <step> <type>
# Records each notice at most once per run, event type and step.
workflow_plan_notify() (
    _wpn_dir="$1" _wpn_step="$2" _wpn_type="$3"
    [ -f "$_wpn_dir/planning-head" ] || exit 0
    _wpn_run="$(sed -n '1p' "$_wpn_dir/run-id")"
    _wpn_text="$(workflow_plan_notice_text "$_wpn_run" "$_wpn_type" "$_wpn_step")"
    [ -n "$_wpn_text" ] || exit 0
    _wpn_key="$(printf '%s.%s' "$_wpn_type" "${_wpn_step:-run}" | tr -c 'A-Za-z0-9._-' '_')"
    mkdir -p "$_wpn_dir/planning-notices" || exit 0
    # Bounded: a run records at most 64 notices.
    [ "$(find "$_wpn_dir/planning-notices" -type d | wc -l)" -le 64 ] || exit 0
    mkdir "$_wpn_dir/planning-notices/$_wpn_key" 2>/dev/null || exit 0
    _wpn_head="$(sed -n '1p' "$_wpn_dir/planning-head")"
    _wpn_instance="$(sed -n '1p' "$_wpn_dir/planning-instance")"
    _wpn_branch="$(sed -n '1p' "$_wpn_dir/planning-branch")"
    _wpn_project="$(sed -n '1p' "$_wpn_dir/project-id")"
    _wpn_head_dir="$(state_v2_head_dir "$_wpn_project" "$_wpn_head")" || exit 0
    # Exact association: the branch still names that head and instance.
    if [ "$(state_v2_find_head_by_branch "$_wpn_project" "$_wpn_branch" 2>/dev/null)" != "$_wpn_head" ] ||
       [ "$(sed -n '1p' "$_wpn_head_dir/current-instance" 2>/dev/null)" != "$_wpn_instance" ]; then
        : > "$_wpn_dir/planning-notices/$_wpn_key/skipped-instance-changed"
        exit 0
    fi
    command -v send_message >/dev/null 2>&1 || _load_lib messages
    _wpn_id="$(send_message "$_wpn_branch" "$_wpn_text" hydra note inbox 2>/dev/null)" || exit 0
    workflow_atomic_scalar "$_wpn_dir/planning-notices/$_wpn_key/message-id" "$_wpn_id" || exit 0
)
