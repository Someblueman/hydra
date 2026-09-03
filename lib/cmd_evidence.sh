#!/bin/sh
# Lifecycle, evidence wait, adapter ingest, and resume commands.

cmd_lifecycle() {
    _cl_branch="${1:-}"
    if [ -z "$_cl_branch" ] || [ "$_cl_branch" = --json ]; then
        cli_error lifecycle invalid_input "head is required" "run hydra lifecycle <head> --json"
        return 1
    fi
    shift
    _cl_json=0
    if [ $# -eq 0 ]; then
        :
    elif [ $# -eq 1 ] && [ "$1" = --json ]; then
        _cl_json=1
    else
        cli_error lifecycle invalid_input "unexpected arguments" "run hydra lifecycle <head> --json"
        return 1
    fi
    lifecycle_load_head "$_cl_branch" || { cli_error lifecycle not_found "Head '$_cl_branch' is unavailable" "inspect with hydra list"; return 1; }
    _cl_outcome="$(lifecycle_read declared-outcome)"
    _cl_observed="$(lifecycle_read observed-status)"
    _cl_confidence="$(lifecycle_read observed-confidence)"
    _cl_liveness="$(lifecycle_liveness "$_cl_branch")" || return 1
    _cl_policy="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/completion-policy" 2>/dev/null || true)"
    if lifecycle_completion_satisfied "$_cl_branch"; then _cl_complete=true; else _cl_complete=false; fi
    if [ "$_cl_json" -eq 1 ]; then
        json_success lifecycle "{\"project_id\":\"$LIFECYCLE_PROJECT_ID\",\"head_id\":\"$LIFECYCLE_HEAD_ID\",\"instance_id\":\"$LIFECYCLE_INSTANCE_ID\",\"branch\":\"$(json_escape "$_cl_branch")\",\"declared_outcome\":$(json_string_or_null "$_cl_outcome"),\"observed_status\":$(json_string_or_null "$_cl_observed"),\"observed_confidence\":$(json_string_or_null "$_cl_confidence"),\"liveness\":\"$_cl_liveness\",\"completion_policy\":\"$(json_escape "${_cl_policy:-declared-done}")\",\"complete\":$_cl_complete}"
    else
        echo "Lifecycle for $_cl_branch"
        echo "  project: $LIFECYCLE_PROJECT_ID"
        echo "  head: $LIFECYCLE_HEAD_ID"
        echo "  instance: $LIFECYCLE_INSTANCE_ID"
        echo "  declared outcome: ${_cl_outcome:-none}"
        echo "  observed status: ${_cl_observed:-unavailable} (${_cl_confidence:-unavailable})"
        echo "  liveness: $_cl_liveness"
        echo "  completion policy: ${_cl_policy:-declared-done}"
        echo "  completion satisfied: $_cl_complete"
    fi
}

cmd_outcome() {
    _co_branch="${1:-}"
    _co_status="${2:-}"
    if [ -z "$_co_branch" ] || [ -z "$_co_status" ]; then
        echo "Usage: hydra outcome <branch> <done|failed|blocked|abandoned|canceled> [--actor <human|agent>] [--summary <text>]" >&2
        return 1
    fi
    shift 2
    _co_actor=human
    _co_summary=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --actor) [ $# -ge 2 ] || return 1; _co_actor="$2"; shift 2 ;;
            --summary) [ $# -ge 2 ] || return 1; _co_summary="$2"; shift 2 ;;
            *) echo "Error: unknown outcome option '$1'" >&2; return 1 ;;
        esac
    done
    lifecycle_set_outcome "$_co_branch" "$_co_status" "$_co_actor" local "$_co_summary" || {
        echo "Error: could not declare outcome '$_co_status'" >&2
        return 1
    }
    echo "Declared $_co_status for $_co_branch (current instance only)"
}

cmd_wait() {
    _cw_branch="${1:-}"
    if [ -z "$_cw_branch" ] || [ "$_cw_branch" = --json ]; then
        cli_error wait invalid_input "head is required" "run hydra wait <head> --json"
        return 1
    fi
    shift
    _cw_condition=complete
    _cw_timeout=300
    _cw_json=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --for) [ $# -ge 2 ] || { cli_error wait invalid_input "--for requires a condition" "inspect hydra lifecycle <head>"; return 1; }; _cw_condition="$2"; shift 2 ;;
            --timeout) [ $# -ge 2 ] || { cli_error wait invalid_input "--timeout requires seconds" "pass a non-negative integer"; return 1; }; _cw_timeout="$2"; shift 2 ;;
            --json) _cw_json=1; shift ;;
            *) cli_error wait invalid_input "unknown wait option '$1'" "run hydra wait <head> --json"; return 1 ;;
        esac
    done
    case "$_cw_timeout" in ''|*[!0-9]*) cli_error wait invalid_input "timeout must be a non-negative integer" "pass --timeout <seconds>"; return 1 ;; esac
    lifecycle_load_head "$_cw_branch" || { cli_error wait not_found "Head '$_cw_branch' is unavailable" "inspect with hydra list"; return 1; }
    _cw_instance="$LIFECYCLE_INSTANCE_ID"
    _cw_started="$(date +%s)"
    while :; do
        lifecycle_load_head "$_cw_branch" || return 1
        if [ "$LIFECYCLE_INSTANCE_ID" != "$_cw_instance" ]; then
            if [ "$_cw_json" -eq 1 ]; then
                json_error wait instance_changed "Current instance changed while waiting" "inspect with hydra lifecycle $_cw_branch"
            else
                echo "Error: current instance changed while waiting" >&2
            fi
            return 3
        fi
        if lifecycle_condition_met "$_cw_branch" "$_cw_condition"; then
            _cw_result=0
        else
            _cw_result=$?
        fi
        if [ "$_cw_result" -eq 0 ]; then
            _cw_elapsed=$(($(date +%s) - _cw_started))
            if [ "$_cw_json" -eq 1 ]; then
                json_success wait "{\"branch\":\"$(json_escape "$_cw_branch")\",\"instance_id\":\"$_cw_instance\",\"condition\":\"$(json_escape "$_cw_condition")\",\"elapsed_seconds\":$_cw_elapsed}"
            else
                echo "Satisfied: $_cw_branch $_cw_condition (instance $_cw_instance)"
            fi
            return 0
        fi
        [ "$_cw_result" -ne 2 ] || return 1
        _cw_elapsed=$(($(date +%s) - _cw_started))
        if [ "$_cw_elapsed" -ge "$_cw_timeout" ]; then
            if [ "$_cw_json" -eq 1 ]; then
                json_error wait timeout "Timed out waiting for $_cw_condition" "inspect with hydra lifecycle $_cw_branch"
            else
                echo "Timed out waiting for $_cw_branch $_cw_condition" >&2
            fi
            return 2
        fi
        sleep 1
    done
}

cmd_adapter() {
    [ "${1:-}" = ingest ] || { echo "Usage: hydra adapter ingest <branch>" >&2; return 1; }
    _ca_branch="${2:-}"
    if [ -z "$_ca_branch" ] || [ $# -ne 2 ]; then
        echo "Usage: hydra adapter ingest <branch>" >&2
        return 1
    fi
    _ca_input="$(LC_ALL=C dd bs=8193 count=1 2>/dev/null)"
    _ca_bytes="$(printf '%s' "$_ca_input" | LC_ALL=C wc -c | tr -d ' ')"
    [ "$_ca_bytes" -le 8192 ] || { echo "Error: adapter input exceeds 8192 bytes" >&2; return 1; }
    _ca_compact="$(printf '%s' "$_ca_input" | tr -d ' \t\r\n')"
    _ca_instance="$(printf '%s' "$_ca_compact" | sed -n 's/^{"schema_version":1,"instance_id":"\([a-z0-9_]*\)","kind":"[a-z]*","status":"[a-z-]*"}$/\1/p')"
    _ca_kind="$(printf '%s' "$_ca_compact" | sed -n 's/^{"schema_version":1,"instance_id":"[a-z0-9_]*","kind":"\([a-z]*\)","status":"[a-z-]*"}$/\1/p')"
    _ca_status="$(printf '%s' "$_ca_compact" | sed -n 's/^{"schema_version":1,"instance_id":"[a-z0-9_]*","kind":"[a-z]*","status":"\([a-z-]*\)"}$/\1/p')"
    if [ -z "$_ca_instance" ] || [ -z "$_ca_kind" ] || [ -z "$_ca_status" ]; then
        echo "Error: malformed adapter event; use canonical adapter JSON v1" >&2
        return 1
    fi
    lifecycle_load_head "$_ca_branch" || return 1
    [ "$_ca_instance" = "$LIFECYCLE_INSTANCE_ID" ] || {
        echo "Error: stale adapter event for non-current instance" >&2
        return 1
    }
    case "$_ca_kind" in
        observed) lifecycle_set_observed "$_ca_branch" "$_ca_status" adapter reported ;;
        outcome) lifecycle_set_outcome "$_ca_branch" "$_ca_status" adapter generic-ingest ;;
        *) echo "Error: adapter kind must be observed or outcome" >&2; return 1 ;;
    esac || { echo "Error: unsupported adapter status '$_ca_status'" >&2; return 1; }
    echo "Accepted adapter event for $_ca_branch instance $_ca_instance"
}

_cmd_resume_abort() {
    _cra_branch="$1"
    _cra_session="$2"
    tmux kill-session -t "$_cra_session" 2>/dev/null || true
    release_session_lock "$_cra_session" 2>/dev/null || true
    lifecycle_abort_new_instance "$_cra_branch" 2>/dev/null || {
        echo "Error: failed to restore the prior lifecycle instance" >&2
    }
    return 1
}

cmd_resume() {
    _cr_branch="${1:-}"
    if [ -z "$_cr_branch" ] || [ $# -ne 1 ]; then
        echo "Usage: hydra resume <branch>" >&2
        return 1
    fi
    lifecycle_load_head "$_cr_branch" || return 1
    _cr_old_instance="$LIFECYCLE_INSTANCE_ID"
    _cr_old_dir="$LIFECYCLE_INSTANCE_DIR"
    _cr_old_session="$(sed -n '1p' "$_cr_old_dir/session" 2>/dev/null || true)"
    if [ -n "$_cr_old_session" ] && tmux has-session -t "$_cr_old_session" 2>/dev/null; then
        echo "Error: head '$_cr_branch' is already live in $_cr_old_session" >&2
        return 1
    fi
    _cr_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree" 2>/dev/null || true)"
    [ -n "$_cr_worktree" ] || { echo "Error: head has no stored worktree path" >&2; return 1; }
    if [ ! -d "$_cr_worktree" ]; then
        create_worktree "$_cr_branch" "$_cr_worktree" || return 1
    fi
    _cr_profile="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/profile" 2>/dev/null || true)"
    if [ -z "$_cr_profile" ] || [ "$_cr_profile" = - ]; then
        _cr_profile=none
    fi
    _cr_provider="$(sed -n '1p' "$_cr_old_dir/provider-session-id" 2>/dev/null || true)"
    _cr_recipe=""
    if [ "$_cr_profile" != none ]; then
        profile_executable_path "$_cr_profile" >/dev/null || {
            echo "Error: profile '$_cr_profile' is unavailable; use --no-agent at initial spawn or install it" >&2
            return 1
        }
        _cr_recipe="$(profile_resume_command "$_cr_profile" "$_cr_provider")" || {
            echo "Error: profile '$_cr_profile' has no supported resume recipe" >&2
            return 1
        }
    fi
    _cr_session="$(generate_session_name "$_cr_branch")" || return 1
    create_session "$_cr_session" "$_cr_worktree" || return 1
    release_session_lock "$_cr_session" 2>/dev/null || true
    if ! lifecycle_new_instance "$_cr_branch" "$_cr_session" "$_cr_provider" "$_cr_recipe"; then
        tmux kill-session -t "$_cr_session" 2>/dev/null || true
        return 1
    fi
    provenance_capture_instance "$_cr_branch" resume "$_cr_recipe" || {
        _cmd_resume_abort "$_cr_branch" "$_cr_session"
        return 1
    }
    for _cr_pair in \
        "HYDRA_PROJECT_ID=$LIFECYCLE_PROJECT_ID" \
        "HYDRA_HEAD_ID=$LIFECYCLE_HEAD_ID" \
        "HYDRA_INSTANCE_ID=$LIFECYCLE_INSTANCE_ID" \
        "HYDRA_BRANCH=$_cr_branch" \
        "HYDRA_WORKTREE=$_cr_worktree" \
        "HYDRA_STATE_DIR=$LIFECYCLE_HEAD_DIR" \
        "HYDRA_TASK_FILE=$LIFECYCLE_HEAD_DIR/task"; do
        tmux set-environment -t "$_cr_session" "${_cr_pair%%=*}" "${_cr_pair#*=}" 2>/dev/null || true
    done
    _cr_export="export HYDRA_PROJECT_ID=$(profile_shell_quote "$LIFECYCLE_PROJECT_ID") HYDRA_HEAD_ID=$(profile_shell_quote "$LIFECYCLE_HEAD_ID") HYDRA_INSTANCE_ID=$(profile_shell_quote "$LIFECYCLE_INSTANCE_ID") HYDRA_BRANCH=$(profile_shell_quote "$_cr_branch") HYDRA_WORKTREE=$(profile_shell_quote "$_cr_worktree") HYDRA_STATE_DIR=$(profile_shell_quote "$LIFECYCLE_HEAD_DIR") HYDRA_TASK_FILE=$(profile_shell_quote "$LIFECYCLE_HEAD_DIR/task")"
    send_keys_to_session "$_cr_session" "$_cr_export" || {
        _cmd_resume_abort "$_cr_branch" "$_cr_session"
        return 1
    }
    if [ -n "$_cr_recipe" ] && ! send_keys_to_session "$_cr_session" "$_cr_recipe"; then
        _cmd_resume_abort "$_cr_branch" "$_cr_session"
        return 1
    fi
    event_emit "$LIFECYCLE_PROJECT_ID" "$LIFECYCLE_HEAD_ID" "$LIFECYCLE_INSTANCE_ID" lifecycle.resumed hydra local \
        "{\"previous_instance_id\":\"$_cr_old_instance\",\"profile\":\"$(json_escape "$_cr_profile")\"}" >/dev/null || {
        _cmd_resume_abort "$_cr_branch" "$_cr_session"
        return 1
    }
    lifecycle_set_observed "$_cr_branch" running hydra exact || {
        _cmd_resume_abort "$_cr_branch" "$_cr_session"
        return 1
    }
    lifecycle_commit_new_instance || echo "Warning: could not remove resume rollback metadata" >&2
    echo "Resumed $_cr_branch in $_cr_session (instance $LIFECYCLE_INSTANCE_ID)"
}
