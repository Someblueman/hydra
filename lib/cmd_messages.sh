#!/bin/sh
# Hydra message command handlers
# POSIX-compliant shell script

cmd_send() {
    msg_type=note
    delivery=inbox
    while [ $# -gt 0 ]; do
        case "$1" in
            --type) [ $# -ge 2 ] || return 1; msg_type="$2"; shift 2 ;;
            --delivery) [ $# -ge 2 ] || return 1; delivery="$2"; shift 2 ;;
            *) break ;;
        esac
    done
    if [ $# -lt 2 ]; then
        echo "Error: Target branch and message required" >&2
        echo "Usage: hydra send <branch> <message>" >&2
        return 1
    fi

    target="$1"
    shift
    message="$*"

    # Validate target has a session (warn only)
    session="$(get_session_for_branch "$target" 2>/dev/null || true)"
    if [ -z "$session" ]; then
        echo "Warning: No active session for '$target'" >&2
    fi

    if message_id="$(send_message "$target" "$message" "" "$msg_type" "$delivery")"; then
        echo "Message $message_id queued for '$target' [$msg_type/$delivery]"
    else
        echo "Error: Failed to send message" >&2
        return 1
    fi
}

cmd_recv() {
    # Receive messages for current session
    peek=0
    json_output=""
    receipts=0
    receipt_branch=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --peek)
                peek=1
                shift
                ;;
            -j|--json)
                json_output="1"
                shift
                ;;
            --receipts)
                receipts=1
                shift
                if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then receipt_branch="$1"; shift; fi
                ;;
            -*)
                cli_error recv invalid_input "Unknown option '$1'" "run hydra recv --json"
                return 1
                ;;
            *)
                cli_error recv invalid_input "Unexpected argument '$1'" "run hydra recv --json"
                return 1
                ;;
        esac
    done

    if [ "$receipts" -eq 1 ]; then
        [ -n "$receipt_branch" ] || receipt_branch="$(get_current_branch 2>/dev/null || true)"
        [ -n "$receipt_branch" ] || { cli_error receipts invalid_input "receipt branch required outside a Hydra session" "pass hydra recv --receipts <branch> --json"; return 1; }
        if [ -n "$json_output" ]; then message_receipts "$receipt_branch" --json; else message_receipts "$receipt_branch"; fi
        return $?
    fi

    # Get current branch
    current_session="$(get_current_session 2>/dev/null || true)"
    if [ -z "$current_session" ]; then
        cli_error recv not_in_session "Not in a Hydra session" "run inside a Hydra tmux session"
        return 1
    fi

    branch="$(get_branch_for_session "$current_session" 2>/dev/null || true)"
    if [ -z "$branch" ]; then
        cli_error recv state_unavailable "Cannot determine branch for current session" "run hydra state verify"
        return 1
    fi

    if [ -n "$json_output" ]; then
        # JSON output mode
        msg_dir="$(get_message_dir "$branch")"
        queue_dir="$msg_dir/queue"

        printf '{"schema_version":1,"ok":true,"command":"recv","data":{"branch":"%s","messages":[' "$(json_escape "$branch")"

        first=1
        if [ -d "$queue_dir" ]; then
            for msg_file in "$queue_dir"/*; do
                [ -f "$msg_file" ] || continue
                msg_lock="$(get_message_lock "$branch")"
                acquire_lock "$msg_lock" "message receive json" || return 1
                [ -f "$msg_file" ] || { release_lock "$msg_lock"; continue; }

                filename="$(basename "$msg_file")"
                timestamp="${filename%%_*}"
                sender="${filename#*_}"
                sender="${sender%_*}"
                message="$(cat "$msg_file")"
                meta_file="$msg_dir/metadata/$filename"
                msg_type="$(sed -n 's/^type=//p' "$meta_file" 2>/dev/null || true)"
                delivery="$(sed -n 's/^delivery=//p' "$meta_file" 2>/dev/null || true)"
                target_instance="$(sed -n 's/^target_instance=//p' "$meta_file" 2>/dev/null || true)"
                msg_type="${msg_type:-note}"
                delivery="${delivery:-inbox}"
                current_instance=""
                if command -v hydra_get_project_id >/dev/null 2>&1; then
                    _crj_project="$(hydra_get_project_id 2>/dev/null || true)"
                    _crj_head="$(state_v2_find_head_by_branch "$_crj_project" "$branch" 2>/dev/null || true)"
                    if [ -n "$_crj_head" ]; then
                        _crj_dir="$(state_v2_head_dir "$_crj_project" "$_crj_head")"
                        current_instance="$(sed -n '1p' "$_crj_dir/current-instance" 2>/dev/null || true)"
                    fi
                fi
                if [ -n "$target_instance" ] && [ "$target_instance" != "$current_instance" ]; then
                    _message_write_receipt_locked "$msg_dir" "$filename" stale "$target_instance" || { release_lock "$msg_lock"; return 1; }
                    mv "$msg_file" "$msg_dir/archive/$filename" 2>/dev/null || rm -f "$msg_file"
                    release_lock "$msg_lock"
                    continue
                fi

                if [ "$first" -eq 1 ]; then
                    first=0
                else
                    printf ','
                fi

                printf '{"message_id":"%s","from": "%s", "timestamp": %s, "type":"%s","delivery":"%s","target_instance":%s,"message": "%s"}' \
                    "$(json_escape "$filename")" \
                    "$(json_escape "$sender")" \
                    "$timestamp" \
                    "$(json_escape "$msg_type")" \
                    "$(json_escape "$delivery")" \
                    "$(json_string_or_null "$target_instance")" \
                    "$(json_escape "$message")"

                # Remove if not peeking
                if [ "$peek" -eq 0 ]; then
                    rm -f "$msg_file"
                    if [ -f "$meta_file" ]; then
                        _message_write_receipt_locked "$msg_dir" "$filename" delivered "$current_instance" || { release_lock "$msg_lock"; return 1; }
                    fi
                fi
                release_lock "$msg_lock"
            done
        fi

        printf ']}}\n'
    else
        # Human-readable output
        recv_opts=""
        [ "$peek" -eq 1 ] && recv_opts="--peek"

        if recv_messages "$branch" $recv_opts; then
            : # Messages printed by recv_messages
        else
            echo "No messages"
        fi
    fi
}
