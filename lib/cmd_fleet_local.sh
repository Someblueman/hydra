#!/bin/sh
# Host-local instance checks and tmux actions remain shell policy.
cmd_fleet_local() (
    export HYDRA_JSON_REQUESTED=1
    if [ $# -lt 3 ] || [ $# -gt 4 ]; then
        cli_error fleet-local invalid_input 'expected action, branch, instance, and optional signal' 'use hydra fleet help'
        exit 1
    fi
    _fl_action="$1"; _fl_branch="$2"; _fl_instance="$3"
    case "$_fl_action" in session|signal|cancel) ;; *) exit 1 ;; esac
    _lifecycle_load_head_locked "$_fl_branch" 'fleet head action' || exit 1
    trap '_lifecycle_release_head_lock' 0
    trap 'exit 130' INT
    trap 'exit 143' HUP TERM
    if [ "$LIFECYCLE_INSTANCE_ID" != "$_fl_instance" ]; then
        cli_error fleet-local stale_instance 'head instance changed' 'refresh fleet list before acting'
        exit 1
    fi
    if [ "$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/desired-state")" != running ]; then
        cli_error fleet-local stopped_head 'head is not desired running' 'refresh remote lifecycle'
        exit 1
    fi
    _fl_session="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/session")"
    tmux has-session -t "=$_fl_session" 2>/dev/null || {
        cli_error fleet-local offline 'head session is not live' 'inspect remote lifecycle'; exit 1;
    }
    if [ "$_fl_action" = session ]; then
        json_success fleet-local "{\"session\":\"$(json_escape "$_fl_session")\"}"
        exit
    fi
    _fl_signal="${4:-INT}"
    case "$_fl_signal" in INT) _fl_key=C-c ;; *)
        cli_error fleet-local unsupported_signal 'only foreground interrupt (INT) is supported' 'use workflow cancel for workflow cancellation'; exit 1 ;;
    esac
    tmux send-keys -t "$_fl_session:0.0" "$_fl_key" || exit 1
    json_success fleet-local "{\"instance\":\"$LIFECYCLE_INSTANCE_ID\",\"signal\":\"INT\",\"delivered\":true}"
)
