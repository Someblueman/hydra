#!/bin/sh
# tmux helper functions for Hydra
# POSIX-compliant shell script

# Validate AI command against allowlist
# Usage: validate_ai_command <command>
# Returns: 0 if valid, 1 if invalid
validate_ai_command() {
    command="$1"
    
    if [ -z "$command" ]; then
        echo "Error: AI command cannot be empty" >&2
        return 1
    fi
    
    case "$command" in
        "claude"|"codex"|"cursor"|"copilot"|"aider"|"gemini")
            return 0
            ;;
        *)
            echo "Error: Unsupported AI command: $command" >&2
            echo "Supported: claude, codex, cursor, copilot, aider, gemini" >&2
            return 1
            ;;
    esac
}

# Check if tmux is available and meets version requirement
# Usage: check_tmux_version
# Returns: 0 if tmux >= 3.0, 1 otherwise
check_tmux_version() {
    if ! command -v tmux >/dev/null 2>&1; then
        echo "Error: tmux not found in PATH" >&2
        return 1
    fi
    
    # Get tmux version
    version="$(tmux -V | cut -d' ' -f2)"
    major="$(echo "$version" | cut -d'.' -f1)"
    
    # Convert to number for comparison (handle versions like "3.2a")
    major_num="$(echo "$major" | sed 's/[^0-9]//g')"
    
    if [ "$major_num" -lt 3 ]; then
        echo "Error: tmux version $version is too old (need >= 3.0)" >&2
        return 1
    fi
    
    return 0
}

# Check if a tmux session exists
# Usage: tmux_session_exists <session_name>
# Returns: 0 if exists, 1 if not
tmux_session_exists() {
    session="$1"
    if [ -z "$session" ]; then
        return 1
    fi

    if [ -n "${_TMUX_SNAPSHOT_LOADED:-}" ]; then
        printf '%s\n' "$_TMUX_SNAPSHOT_SESSIONS" | grep -Fqx "$session"
        return $?
    fi

    tmux has-session -t "$session" 2>/dev/null
}

# Load a one-shot snapshot of sessions and panes for batch observation.
# Usage: tmux_load_snapshot
# Sets: _TMUX_SNAPSHOT_LOADED, _TMUX_SNAPSHOT_SESSIONS, _TMUX_SNAPSHOT_PANES
# Pane lines: session<TAB>window_index<TAB>pane_index<TAB>window_activity<TAB>pane_current_command<TAB>pane_dead
tmux_load_snapshot() {
    _TMUX_SNAPSHOT_SESSIONS="$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)"
    _TMUX_SNAPSHOT_PANES="$(tmux list-panes -a -F '#{session_name}	#{window_index}	#{pane_index}	#{window_activity}	#{pane_current_command}	#{pane_dead}' 2>/dev/null || true)"
    _TMUX_SNAPSHOT_LOADED=1
}

# Clear a previously loaded tmux snapshot (callers fall back to live probes).
# Usage: tmux_clear_snapshot
tmux_clear_snapshot() {
    _TMUX_SNAPSHOT_LOADED=""
    _TMUX_SNAPSHOT_SESSIONS=""
    _TMUX_SNAPSHOT_PANES=""
}

# Latest window_activity timestamp for a session from the snapshot.
# Usage: tmux_snapshot_window_activity <session>
# Returns: unix timestamp or 0
tmux_snapshot_window_activity() {
    _sa_session="$1"
    if [ -z "${_TMUX_SNAPSHOT_PANES:-}" ]; then
        printf '%s' "0"
        return 0
    fi
    printf '%s\n' "$_TMUX_SNAPSHOT_PANES" | awk -F '	' -v s="$_sa_session" '
        $1 == s {
            act = $4 + 0
            if (act > max) max = act
        }
        END { print max + 0 }
    '
}

# True if every pane in the session is marked pane_dead.
# Usage: tmux_snapshot_session_dead <session>
# Returns: 0 if all panes dead, 1 otherwise
tmux_snapshot_session_dead() {
    _sd_session="$1"
    if [ -z "${_TMUX_SNAPSHOT_PANES:-}" ]; then
        return 1
    fi
    printf '%s\n' "$_TMUX_SNAPSHOT_PANES" | awk -F '	' -v s="$_sd_session" '
        $1 == s { seen = 1; if ($6 != "1") live = 1 }
        END { if (seen && !live) exit 0; exit 1 }
    '
}

# WARNING: SECURITY SENSITIVE
# This function executes arbitrary commands in tmux sessions.
# Only call with trusted, validated input - never with user input.
# Commands are executed with the user's shell privileges.
#
# Send keys to an explicit tmux target (session:window.pane).
# Usage: send_keys_to_target <session_name> <pane_target> <keys>
# Returns: 0 on success, 1 on failure
send_keys_to_target() {
    session="$1"
    pane_target="$2"
    keys="$3"

    if [ -z "$session" ] || [ -z "$pane_target" ] || [ -z "$keys" ]; then
        echo "Error: Session name, pane target, and keys are required" >&2
        return 1
    fi

    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi

    tmux send-keys -t "$pane_target" "$keys" Enter || return 1

    return 0
}

# Send keys to the primary pane (session:0.0), never "whatever is active".
# Usage: send_keys_to_session <session_name> <keys>
# Returns: 0 on success, 1 on failure
send_keys_to_session() {
    session="$1"
    keys="$2"

    if [ -z "$session" ] || [ -z "$keys" ]; then
        echo "Error: Session name and keys are required" >&2
        return 1
    fi

    send_keys_to_target "$session" "${session}:0.0" "$keys"
}

# Resolve a shell pane for broadcast. Prefer a non-agent pane.
# Usage: find_broadcast_pane <session> [ai_tool]
# Prints session:window.pane on stdout. Returns 1 if only the agent pane exists.
find_broadcast_pane() {
    _fb_session="$1"
    _fb_ai="${2:-}"

    if [ -z "$_fb_session" ]; then
        return 1
    fi

    _fb_panes="$(tmux list-panes -t "$_fb_session" -F '#{window_index}.#{pane_index} #{pane_current_command}' 2>/dev/null || true)"
    if [ -z "$_fb_panes" ]; then
        return 1
    fi

    _fb_shell=""
    _fb_has_agent_slot=0
    if [ -n "$_fb_ai" ] && [ "$_fb_ai" != "-" ]; then
        _fb_has_agent_slot=1
    fi

    while IFS=' ' read -r _fb_idx _fb_cmd; do
        [ -n "$_fb_idx" ] || continue
        _fb_is_shell=0
        case "$_fb_cmd" in
            sh|bash|dash|zsh|fish|-sh|-bash|-dash|-zsh|-fish)
                _fb_is_shell=1
                ;;
        esac
        _fb_is_agent=0
        case "$_fb_cmd" in
            claude|codex|cursor|copilot|aider|gemini)
                _fb_is_agent=1
                ;;
        esac
        # Default spawn stores "-" for AI but still launches the agent on :0.0.
        # Treat a live agent process on the primary pane as an agent slot.
        if [ "$_fb_idx" = "0.0" ] && [ "$_fb_is_agent" -eq 1 ]; then
            _fb_has_agent_slot=1
            continue
        fi
        if [ "$_fb_idx" = "0.0" ] && [ "$_fb_has_agent_slot" -eq 1 ]; then
            continue
        fi
        if [ "$_fb_is_shell" -eq 1 ]; then
            _fb_shell="${_fb_session}:${_fb_idx}"
            break
        fi
        if [ "$_fb_has_agent_slot" -eq 1 ] && [ "$_fb_idx" != "0.0" ]; then
            # Non-primary pane even if command is not a classic shell name
            _fb_shell="${_fb_session}:${_fb_idx}"
            break
        fi
    done <<EOF
$_fb_panes
EOF

    if [ -n "$_fb_shell" ]; then
        printf '%s\n' "$_fb_shell"
        return 0
    fi

    if [ "$_fb_has_agent_slot" -eq 1 ]; then
        return 1
    fi

    printf '%s\n' "${_fb_session}:0.0"
    return 0
}

# Create a new tmux session
# Usage: create_session <session_name> <start_directory>
# Returns: 0 on success, 1 on failure
create_session() {
    session="$1"
    start_dir="$2"
    
    if [ -z "$session" ] || [ -z "$start_dir" ]; then
        echo "Error: Session name and directory are required" >&2
        return 1
    fi
    
    if ! [ -d "$start_dir" ]; then
        echo "Error: Directory does not exist: $start_dir" >&2
        return 1
    fi
    
    if tmux_session_exists "$session"; then
        echo "Error: Session already exists: $session" >&2
        return 1
    fi
    
    # Create detached session with specified working directory
    tmux new-session -d -s "$session" -c "$start_dir" || return 1
    
    return 0
}

# Kill a tmux session
# Usage: kill_session <session_name>
# Returns: 0 on success, 1 on failure
kill_session() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi
    
    tmux kill-session -t "$session" || return 1
    
    return 0
}

# List all tmux sessions
# Usage: list_sessions
# Returns: Session names on stdout
list_sessions() {
    tmux list-sessions -F '#{session_name}' 2>/dev/null || true
}

# Switch to a tmux session
# Usage: switch_to_session <session_name>
# Returns: 0 on success, 1 on failure
switch_to_session() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi
    
    # Check if we're inside tmux
    if [ -n "${TMUX:-}" ]; then
        # Inside tmux, use switch-client
        tmux switch-client -t "$session" || return 1
    else
        # Outside tmux, attach to session
        tmux attach-session -t "$session" || return 1
    fi
    
    return 0
}

# Get current tmux session name
# Usage: get_current_session
# Returns: Session name on stdout, empty if not in tmux
get_current_session() {
    if [ -z "${TMUX:-}" ]; then
        return 1
    fi
    
    tmux display-message -p '#{session_name}' 2>/dev/null || true
}

# Rename a tmux session
# Usage: rename_session <old_name> <new_name>
# Returns: 0 on success, 1 on failure
rename_session() {
    old_name="$1"
    new_name="$2"
    
    if [ -z "$old_name" ] || [ -z "$new_name" ]; then
        echo "Error: Both old and new session names are required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$old_name"; then
        echo "Error: Session does not exist: $old_name" >&2
        return 1
    fi
    
    if tmux_session_exists "$new_name"; then
        echo "Error: Target session name already exists: $new_name" >&2
        return 1
    fi
    
    tmux rename-session -t "$old_name" "$new_name" || return 1
    
    return 0
}
