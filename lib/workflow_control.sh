#!/bin/sh
# Workspace transport for existing, explicitly requested lifecycle operations.
workflow_control() (
    [ "$#" -eq 3 ] && hydra_valid_id "$1" || exit 2
    _wct_run="$1" _wct_action="$2" _wct_request="$3"
    case "$_wct_action" in
        resume|cancel) [ "$_wct_request" = - ] || exit 2 ;;
        approve|reject) hydra_valid_id "$_wct_request" || exit 2 ;;
        *) exit 2 ;;
    esac
    _wct_dir="$(workflow_runs_dir)/$_wct_run"
    [ -d "$_wct_dir" ] || exit 1
    umask 077
    exec </dev/null >> "$_wct_dir/workspace-controls.log" 2>&1
    printf '\n%s: %s run=%s request=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$_wct_action" "$_wct_run" "$_wct_request"
    case "$_wct_action" in
        resume|cancel) cmd_workflow "$_wct_action" "$_wct_run" ;;
        approve|reject) cmd_workflow decide "$_wct_run" "$_wct_request" "$_wct_action" --by workspace ;;
    esac
    _wct_exit=$?
    printf 'Control finished: exit %s\n' "$_wct_exit"
    exit "$_wct_exit"
)
