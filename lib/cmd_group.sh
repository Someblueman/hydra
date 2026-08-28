#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_group_create() {
    # Create a group and assign multiple branches to it
    if [ $# -lt 2 ]; then
        echo "Error: Group name and at least one branch required" >&2
        echo "Usage: hydra group create <group-name> <branch> [branch...]" >&2
        return 1
    fi

    group_name="$1"
    shift

    # Validate group name (alphanumeric, dash, underscore only)
    case "$group_name" in
        *[!a-zA-Z0-9_-]*)
            echo "Error: Invalid group name '$group_name' (use alphanumeric, dash, underscore)" >&2
            return 1
            ;;
    esac

    succeeded=0
    failed=0

    for branch in "$@"; do
        # Check if branch has a session
        session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
        if [ -z "$session" ]; then
            echo "Warning: No session found for branch '$branch', skipping" >&2
            failed=$((failed + 1))
            continue
        fi

        if set_group "$branch" "$group_name"; then
            echo "Added '$branch' to group '$group_name'"
            succeeded=$((succeeded + 1))
        else
            echo "Error: Failed to add '$branch' to group" >&2
            failed=$((failed + 1))
        fi
    done

    echo ""
    echo "Group '$group_name': $succeeded added, $failed skipped"

    if [ "$succeeded" -eq 0 ]; then
        return 1
    fi
    return 0
}

cmd_group_wait() {
    # Wait for all sessions in a group to be killed/completed
    group_name=""
    timeout_seconds=3600
    poll_interval=5

    while [ $# -gt 0 ]; do
        case "$1" in
            -t|--timeout)
                shift
                timeout_seconds="${1:-3600}"
                shift
                ;;
            -p|--poll)
                shift
                poll_interval="${1:-5}"
                shift
                ;;
            -*)
                cli_error "group status" invalid_input "Unknown option '$1'" "run hydra group status <name> --json"
                return 1
                ;;
            *)
                if [ -z "$group_name" ]; then
                    group_name="$1"
                else
                    cli_error "group status" invalid_input "Unexpected argument '$1'" "run hydra group status <name> --json"
                    return 1
                fi
                shift
                ;;
        esac
    done

    if [ -z "$group_name" ]; then
        echo "Error: Group name required" >&2
        echo "Usage: hydra group wait <group-name> [-t <timeout>] [-p <poll>]" >&2
        return 1
    fi

    # Check if group has sessions
    mappings="$(list_mappings_for_group "$group_name")"
    if [ -z "$mappings" ]; then
        echo "No sessions found in group '$group_name'"
        return 0
    fi

    # Count total sessions
    total="$(printf '%s\n' "$mappings" | wc -l | tr -d ' ')"
    echo "Waiting for $total session(s) in group '$group_name' to complete..."
    echo "Timeout: ${timeout_seconds}s, Poll interval: ${poll_interval}s"

    start_time="$(date +%s)"

    while true; do
        now="$(date +%s)"
        elapsed=$((now - start_time))

        # Check timeout
        if [ "$elapsed" -ge "$timeout_seconds" ]; then
            echo ""
            echo "Error: Timeout after ${elapsed}s" >&2
            return 1
        fi

        # Count active sessions
        active=0
        pending=""
        # Re-read mappings in case sessions were killed
        mappings="$(list_mappings_for_group "$group_name")"

        # Cache tmux sessions once per poll iteration (perf: avoid N subprocess calls)
        _cached_tmux_sessions="$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)"
        _session_exists_cached() {
            echo "$_cached_tmux_sessions" | grep -qx "$1" 2>/dev/null
        }

        if [ -n "$mappings" ]; then
            # Use a temp file to avoid subshell variable scope issues
            tmp_active="${HYDRA_HOME}/tmp_wait_$$"
            : > "$tmp_active"

            printf '%s\n' "$mappings" | while IFS=' ' read -r branch session _rest; do
                if _session_exists_cached "$session"; then
                    echo "$branch" >> "$tmp_active"
                fi
            done

            if [ -f "$tmp_active" ] && [ -s "$tmp_active" ]; then
                active="$(wc -l < "$tmp_active" | tr -d ' ')"
                pending="$(tr '\n' ',' < "$tmp_active" | sed 's/,$//')"
            fi
            rm -f "$tmp_active" 2>/dev/null
        fi

        if [ "$active" -eq 0 ]; then
            echo ""
            echo "All sessions in group '$group_name' have completed!"
            return 0
        fi

        printf "\r[%ds] Waiting for %d session(s): %s...    " "$elapsed" "$active" "$pending"

        sleep "$poll_interval"
    done
}

cmd_group_status() {
    # Show health status for a group
    group_name=""
    json_output=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -j|--json)
                json_output="1"
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                return 1
                ;;
            *)
                if [ -z "$group_name" ]; then
                    group_name="$1"
                else
                    echo "Error: Unexpected argument '$1'" >&2
                    return 1
                fi
                shift
                ;;
        esac
    done

    if [ -z "$group_name" ]; then
        cli_error "group status" invalid_input "Group name required" "run hydra group status <name> --json"
        return 1
    fi

    # Get mappings for group
    mappings="$(list_mappings_for_group "$group_name")"

    if [ -z "$mappings" ]; then
        if [ -n "$json_output" ]; then
            printf '{"schema_version":1,"ok":true,"command":"group status","data":{"group":"%s","sessions":[],"total":0,"active":0,"dead":0}}\n' \
                "$(json_escape "$group_name")"
        else
            echo "No sessions found in group '$group_name'"
        fi
        return 0
    fi

    # Count stats
    total="$(printf '%s\n' "$mappings" | wc -l | tr -d ' ')"
    active=0
    dead=0

    # Build session info using temp file to avoid subshell issues
    tmp_sessions="${HYDRA_HOME}/tmp_status_$$"
    : > "$tmp_sessions"

    printf '%s\n' "$mappings" | while IFS=' ' read -r branch session ai _group timestamp deps pr; do
        if tmux_session_exists "$session"; then
            status="active"
        else
            status="dead"
        fi

        # Calculate duration
        duration_secs=0
        if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
            now="$(date +%s)"
            duration_secs=$((now - timestamp))
        fi

        # Store session info
        echo "$branch|$session|$status|${ai:-"-"}|$duration_secs|${deps:-"-"}|${pr:-"-"}" >> "$tmp_sessions"
    done

    if [ -n "$json_output" ]; then
        # JSON output
        printf '{"schema_version":1,"ok":true,"command":"group status","data":{"group":"%s","sessions":[' "$(json_escape "$group_name")"
        first=1
        while IFS='|' read -r branch session status ai duration_secs deps pr; do
            [ "$first" -eq 1 ] && first=0 || printf ','

            # Handle optional fields
            ai_json="null"
            [ -n "$ai" ] && [ "$ai" != "-" ] && ai_json="\"$(json_escape "$ai")\""

            deps_json="null"
            [ -n "$deps" ] && [ "$deps" != "-" ] && deps_json="\"$(json_escape "$deps")\""

            pr_json="null"
            [ -n "$pr" ] && [ "$pr" != "-" ] && pr_json="$pr"

            printf '{"branch": "%s", "session": "%s", "status": "%s", "ai": %s, "duration_seconds": %s, "deps": %s, "pr": %s}' \
                "$(json_escape "$branch")" \
                "$(json_escape "$session")" \
                "$status" \
                "$ai_json" \
                "$duration_secs" \
                "$deps_json" \
                "$pr_json"

            # Count for summary
            case "$status" in
                active) active=$((active + 1)) ;;
                dead) dead=$((dead + 1)) ;;
            esac
        done < "$tmp_sessions"
        printf '],"total":%d,"active":%d,"dead":%d}}\n' "$total" "$active" "$dead"
    else
        # Human-readable output
        echo "Group Status: $group_name"
        echo "=========================="
        echo ""

        while IFS='|' read -r branch session status ai duration_secs deps pr; do
            # Build status indicator
            case "$status" in
                active)
                    status_str="[OK]"
                    active=$((active + 1))
                    ;;
                dead)
                    status_str="[DEAD]"
                    dead=$((dead + 1))
                    ;;
            esac

            # Format duration
            duration_str=""
            if [ "$duration_secs" -gt 0 ]; then
                duration_str="$(format_duration "$duration_secs")"
            fi

            # Build info line
            info=""
            [ -n "$duration_str" ] && info="($duration_str)"
            [ -n "$ai" ] && [ "$ai" != "-" ] && info="$info [ai: $ai]"
            [ -n "$deps" ] && [ "$deps" != "-" ] && info="$info [deps: $deps]"
            [ -n "$pr" ] && [ "$pr" != "-" ] && info="$info [PR #$pr]"

            printf "  %-6s %s -> %s %s\n" "$status_str" "$branch" "$session" "$info"
        done < "$tmp_sessions"

        echo ""
        echo "Summary: $total total, $active active, $dead dead"
    fi

    rm -f "$tmp_sessions" 2>/dev/null
}

cmd_group() {
    # Check for subcommand pattern first
    case "${1:-}" in
        create)
            shift
            cmd_group_create "$@"
            return $?
            ;;
        wait)
            shift
            cmd_group_wait "$@"
            return $?
            ;;
        status)
            shift
            cmd_group_status "$@"
            return $?
            ;;
    esac

    # Original logic: Assign or show group for a branch
    if [ $# -lt 1 ]; then
        echo "Error: Branch name or subcommand required" >&2
        echo "Usage: hydra group <branch> [<group-name>]" >&2
        echo "       hydra group <branch> --clear" >&2
        echo "       hydra group create <name> <branch> [branch...]" >&2
        echo "       hydra group wait <name> [-t <timeout>]" >&2
        echo "       hydra group status <name> [--json]" >&2
        return 1
    fi

    branch="$1"
    shift

    # Check if branch exists in mappings
    session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
    if [ -z "$session" ]; then
        echo "Error: No session found for branch '$branch'" >&2
        return 1
    fi

    # If no group specified, show current group
    if [ $# -eq 0 ]; then
        current_group="$(get_group_for_branch "$branch" 2>/dev/null || true)"
        if [ -n "$current_group" ]; then
            echo "Group for '$branch': $current_group"
        else
            echo "No group assigned to '$branch'"
        fi
        return 0
    fi

    # Handle --clear
    if [ "$1" = "--clear" ]; then
        if set_group "$branch" ""; then
            echo "Cleared group for '$branch'"
        else
            echo "Error: Failed to clear group" >&2
            return 1
        fi
        return 0
    fi

    # Set the group
    new_group="$1"
    if set_group "$branch" "$new_group"; then
        echo "Set group for '$branch' to '$new_group'"
    else
        echo "Error: Failed to set group" >&2
        return 1
    fi
}

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
