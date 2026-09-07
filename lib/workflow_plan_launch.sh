#!/bin/sh
# Workspace launch ownership only; admission and scheduling remain in workflow.

workflow_plan_launch_dir() {
    [ "${#1}" -eq 64 ] || return 2
    case "$1" in *[!0-9a-f]*) return 2 ;; esac
    _wpl_project="$(hydra_get_project_id)" || return 1
    _wpl_project_dir="$(state_v2_project_dir "$_wpl_project")" || return 1
    printf '%s/workflows/launches/%s\n' "$_wpl_project_dir" "$1"
}

# stdin is an already-open compiled snapshot. The caller starts this owner in
# an independent process group with SIGHUP ignored and no terminal descriptors.
# A reservation is never retried automatically, even after an uncertain crash.
workflow_plan_launch_owner() (
    [ "$#" -eq 1 ] || exit 2
    _workflow_plan_launch="$(workflow_plan_launch_dir "$1")" || exit $?
    umask 077
    mkdir -p "$(dirname "$_workflow_plan_launch")" || exit 1
    mkdir "$_workflow_plan_launch" 2>/dev/null || exit 1
    exec > "$_workflow_plan_launch/owner.log" 2>&1
    workflow_atomic_scalar "$_workflow_plan_launch/state" starting || exit 1
    if ! cat > "$_workflow_plan_launch/compiled.json"; then
        workflow_atomic_scalar "$_workflow_plan_launch/state" failed
        exit 1
    fi
    cmd_workflow_plan run "$_workflow_plan_launch/compiled.json" --accept "$1"
    _wpl_result=$?
    workflow_atomic_scalar "$_workflow_plan_launch/exit-code" "$_wpl_result"
    workflow_atomic_scalar "$_workflow_plan_launch/state" finished
    exit "$_wpl_result"
)

workflow_plan_launch_status() (
    [ "$#" -eq 1 ] || exit 2
    _wpls_dir="$(workflow_plan_launch_dir "$1")" || exit $?
    _wpls_state=absent _wpls_run=- _wpls_exit=-
    if [ -d "$_wpls_dir" ]; then
        _wpls_state=unknown
        [ ! -f "$_wpls_dir/state" ] || _wpls_state="$(sed -n '1p' "$_wpls_dir/state")"
        [ ! -f "$_wpls_dir/run-id" ] || _wpls_run="$(sed -n '1p' "$_wpls_dir/run-id")"
        [ ! -f "$_wpls_dir/exit-code" ] || _wpls_exit="$(sed -n '1p' "$_wpls_dir/exit-code")"
    fi
    printf 'HYDRA_PLAN_LAUNCH\t1\nL\t%s\t%s\t%s\t%s\n' "$1" "$_wpls_state" "$_wpls_run" "$_wpls_exit"
)
