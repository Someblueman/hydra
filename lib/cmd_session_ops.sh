#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_tail() {
    # Watch output from a session's pane
    lines=50
    follow=false
    branch=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -n|--lines)
                shift
                lines="$1"
                shift
                ;;
            -f|--follow)
                follow=true
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra tail <branch> [-n|--lines <N>] [-f|--follow]" >&2
                return 1
                ;;
            *)
                if [ -z "$branch" ]; then
                    branch="$1"
                else
                    echo "Error: Too many arguments" >&2
                    return 1
                fi
                shift
                ;;
        esac
    done

    if [ -z "$branch" ]; then
        echo "Error: Branch name required" >&2
        echo "Usage: hydra tail <branch> [-n|--lines <N>] [-f|--follow]" >&2
        return 1
    fi

    # Get session for branch
    session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
    if [ -z "$session" ]; then
        echo "Error: No session found for branch '$branch'" >&2
        return 1
    fi

    if ! tmux_session_exists "$session"; then
        echo "Error: Session '$session' is not running" >&2
        return 1
    fi

    if [ "$follow" = true ]; then
        # Follow mode - continuously capture and display
        echo "Tailing session '$session' (Ctrl-C to exit)..."
        echo "---"
        trap 'echo ""; echo "---"; echo "Stopped tailing."; exit 0' INT
        last_output=""
        while true; do
            current_output="$(tmux capture-pane -t "$session" -p -S -"$lines" 2>/dev/null || true)"
            if [ "$current_output" != "$last_output" ]; then
                # Clear screen and show new output
                printf '\033[2J\033[H'
                echo "=== $branch ($session) ==="
                echo "$current_output"
            fi
            last_output="$current_output"
            sleep 1
        done
    else
        # One-shot capture
        echo "=== $branch ($session) - last $lines lines ==="
        tmux capture-pane -t "$session" -p -S -"$lines" 2>/dev/null || true
    fi
}

cmd_broadcast() {
    # Send a command to all sessions or a group
    broadcast_group=""
    command_text=""
    force_pane=""
    explicit_pane=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -g|--group)
                shift
                broadcast_group="$1"
                shift
                ;;
            --force)
                force_pane=1
                shift
                ;;
            --pane)
                shift
                explicit_pane="$1"
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra broadcast [-g|--group <name>] [--pane <target>] [--force] <command>" >&2
                return 1
                ;;
            *)
                # Remaining args are the command
                command_text="$*"
                break
                ;;
        esac
    done

    if [ -z "$command_text" ]; then
        echo "Error: Command required" >&2
        echo "Usage: hydra broadcast [-g|--group <name>] [--pane <target>] [--force] <command>" >&2
        return 1
    fi

    # Get sessions to broadcast to
    if [ -n "$broadcast_group" ]; then
        mappings="$(list_mappings_for_group "$broadcast_group")"
        if [ -z "$mappings" ]; then
            echo "No sessions found in group '$broadcast_group'"
            return 1
        fi
        echo "Broadcasting to group '$broadcast_group'..."
    else
        if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
            echo "No active sessions"
            return 1
        fi
        mappings="$(cat "$HYDRA_MAP")"
        echo "Broadcasting to all sessions..."
    fi

    # Cache all tmux sessions once to avoid repeated subprocess calls (perf)
    _cached_tmux_sessions="$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)"
    _session_exists_cached() {
        echo "$_cached_tmux_sessions" | grep -qx "$1" 2>/dev/null
    }

    # Use temp file for count to avoid subshell variable loss
    tmpcount="$(mktemp)"
    printf "0" > "$tmpcount"

    echo "$mappings" | while IFS=' ' read -r branch session _ai _group; do
        if _session_exists_cached "$session"; then
            _target=""
            if [ -n "$explicit_pane" ]; then
                case "$explicit_pane" in
                    *:*) _target="$explicit_pane" ;;
                    *) _target="${session}:${explicit_pane}" ;;
                esac
            else
                _target="$(find_broadcast_pane "$session" "$_ai" 2>/dev/null || true)"
                if [ -z "$_target" ]; then
                    if [ -n "$force_pane" ]; then
                        _target="${session}:0.0"
                    else
                        echo "  Skipping $branch ($session): no shell pane (agent on :0.0; use --force or --pane)" >&2
                        continue
                    fi
                fi
            fi
            echo "  Sending to $branch ($session) via $_target..."
            tmux send-keys -t "$_target" "$command_text" Enter 2>/dev/null || true
            # Increment count in file
            _cnt="$(cat "$tmpcount")"
            printf "%d" "$((_cnt + 1))" > "$tmpcount"
        fi
    done

    count="$(cat "$tmpcount")"
    rm -f "$tmpcount"

    echo "Sent to $count session(s)"
}

cmd_wait_idle() {
    # Wait for sessions to become idle (no output for N seconds)
    idle_seconds=10
    timeout_seconds=300
    wait_group=""

    while [ $# -gt 0 ]; do
        case "$1" in
            -s|--seconds)
                shift
                idle_seconds="$1"
                shift
                ;;
            -t|--timeout)
                shift
                timeout_seconds="$1"
                shift
                ;;
            -g|--group)
                shift
                wait_group="$1"
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra wait-idle [-g|--group <name>] [-s|--seconds <N>] [-t|--timeout <N>]" >&2
                return 1
                ;;
            *)
                echo "Error: Unexpected argument '$1'" >&2
                return 1
                ;;
        esac
    done

    # Get sessions to monitor
    if [ -n "$wait_group" ]; then
        mappings="$(list_mappings_for_group "$wait_group")"
        if [ -z "$mappings" ]; then
            echo "No sessions found in group '$wait_group'"
            return 1
        fi
        echo "Waiting for group '$wait_group' to become idle..."
    else
        if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
            echo "No active sessions"
            return 0
        fi
        mappings="$(cat "$HYDRA_MAP")"
        echo "Waiting for all sessions to become idle..."
    fi

    echo "Idle threshold: ${idle_seconds}s, timeout: ${timeout_seconds}s"

    start_time="$(date +%s)"

    # Track last output hash for each session
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT INT TERM

    # Cache tmux sessions for initialization (perf)
    _cached_tmux_sessions="$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)"
    _session_exists_cached() {
        echo "$_cached_tmux_sessions" | grep -qx "$1" 2>/dev/null
    }

    # Initialize tracking
    echo "$mappings" | while IFS=' ' read -r branch session _ai _group; do
        if _session_exists_cached "$session"; then
            echo "$start_time" > "$tmpdir/$session.time"
            tmux capture-pane -t "$session" -p 2>/dev/null | cksum > "$tmpdir/$session.hash"
        fi
    done

    while true; do
        current_time="$(date +%s)"
        elapsed=$((current_time - start_time))

        if [ "$elapsed" -ge "$timeout_seconds" ]; then
            echo "Timeout after ${timeout_seconds}s"
            return 1
        fi

        # Refresh session cache once per poll iteration (perf: avoid N subprocess calls)
        _cached_tmux_sessions="$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)"

        # Use file to track idle state across subshell
        echo "idle" > "$tmpdir/status"

        echo "$mappings" | while IFS=' ' read -r _branch session _ai _group; do
            if ! _session_exists_cached "$session"; then
                continue
            fi

            # Get current output hash
            current_hash="$(tmux capture-pane -t "$session" -p 2>/dev/null | cksum)"
            last_hash="$(cat "$tmpdir/$session.hash" 2>/dev/null || echo "")"

            if [ "$current_hash" != "$last_hash" ]; then
                # Output changed, reset idle timer
                echo "$current_time" > "$tmpdir/$session.time"
                echo "$current_hash" > "$tmpdir/$session.hash"
                echo "busy" > "$tmpdir/status"
            else
                # Check if idle long enough
                last_change="$(cat "$tmpdir/$session.time" 2>/dev/null || echo "$current_time")"
                idle_time=$((current_time - last_change))
                if [ "$idle_time" -lt "$idle_seconds" ]; then
                    echo "busy" > "$tmpdir/status"
                fi
            fi
        done

        status="$(cat "$tmpdir/status")"
        if [ "$status" = "idle" ]; then
            echo ""
            echo "All sessions idle for ${idle_seconds}s"
            return 0
        fi

        printf "\r[%ds] Waiting...  " "$elapsed"
        sleep 2
    done
}

