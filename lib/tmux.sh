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
        "claude"|"codex"|"cursor"|"copilot"|"aider")
            return 0
            ;;
        *)
            echo "Error: Unsupported AI command: $command" >&2
            echo "Supported: claude, codex, cursor, copilot, aider" >&2
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
    
    tmux has-session -t "$session" 2>/dev/null
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

# WARNING: SECURITY SENSITIVE
# This function executes arbitrary commands in tmux sessions.
# Only call with trusted, validated input - never with user input.
# Commands are executed with the user's shell privileges.
#
# Send keys to a tmux session
# Usage: send_keys_to_session <session_name> <keys>
# Returns: 0 on success, 1 on failure
send_keys_to_session() {
    session="$1"
    keys="$2"
    
    if [ -z "$session" ] || [ -z "$keys" ]; then
        echo "Error: Session name and keys are required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi
    
    tmux send-keys -t "$session" "$keys" Enter || return 1
    
    return 0
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
    if [ -z "$TMUX" ]; then
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