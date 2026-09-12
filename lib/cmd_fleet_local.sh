#!/bin/sh
# Host-local instance checks and tmux actions remain shell policy.
cmd_fleet_local() (
    export HYDRA_JSON_REQUESTED=1
    if [ $# -lt 3 ] || [ $# -gt 4 ]; then
        cli_error fleet-local invalid_input 'expected action, branch, instance, and optional signal' 'use hydra fleet help'
        exit 1
    fi
    _fl_action="$1"; _fl_branch="$2"; _fl_instance="$3"
    case "$_fl_action" in session|attach|signal|cancel) ;; *) exit 1 ;; esac
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
    if [ "$_fl_action" = session ] || [ "$_fl_action" = attach ]; then
        _fl_session_id="$(tmux display-message -p -t "=$_fl_session:" '#{session_id}' 2>/dev/null || true)"
        [ -n "$_fl_session_id" ] || {
            cli_error fleet-local offline 'head session identity is not available' 'inspect remote lifecycle'; exit 1;
        }
        if [ "$_fl_action" = attach ]; then
            _fl_project_id="$(hydra_get_project_id)" || { cli_error fleet-local invalid_project 'project identity is unavailable' 'refresh the remote project'; exit 1; }
            _lifecycle_release_head_lock
            trap - 0
            # Evaluate the identity and attach in one command queue owned by
            # the target server. A replacement server or session must match
            # all three session environment values before it can receive input.
            _fl_guard="#{&&:#{==:#{HYDRA_PROJECT_ID},$_fl_project_id},#{&&:#{==:#{HYDRA_HEAD_ID},$LIFECYCLE_HEAD_ID},#{==:#{HYDRA_INSTANCE_ID},$_fl_instance}}}"
            tmux if-shell -F -t "=$_fl_session_id:" "$_fl_guard" "attach-session -t '=$_fl_session_id:'" "display-message -p 'Hydra attach refused: session identity changed'"
            exit $?
        fi
        json_success fleet-local "{\"session\":\"$(json_escape "$_fl_session")\",\"session_id\":\"$(json_escape "$_fl_session_id")\"}"
        exit
    fi
    _fl_signal="${4:-INT}"
    case "$_fl_signal" in INT) _fl_key=C-c ;; *)
        cli_error fleet-local unsupported_signal 'only foreground interrupt (INT) is supported' 'use workflow cancel for workflow cancellation'; exit 1 ;;
    esac
    tmux send-keys -t "$_fl_session:0.0" "$_fl_key" || exit 1
    json_success fleet-local "{\"instance\":\"$LIFECYCLE_INSTANCE_ID\",\"signal\":\"INT\",\"delivered\":true}"
)
