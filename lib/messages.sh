#!/bin/sh
# Message queue functions for Hydra
# POSIX-compliant shell script
#
# Provides inter-session messaging via file-based queues.
# Messages are stored in ~/.hydra/messages/<branch>/queue/

# =============================================================================
# Message Directory Management
# =============================================================================

# Get message directory for a branch
# Usage: get_message_dir <branch>
# Returns: Path to message directory on stdout
get_message_dir() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 1
    fi
    # Sanitize branch name for filesystem (same pattern as state.sh)
    safe_branch="$(printf '%s' "$branch" | sed 's/[^a-zA-Z0-9_-]/_/g')"
    echo "${HYDRA_HOME:-$HOME/.hydra}/messages/$safe_branch"
}

# Ensure message directories exist for a branch
# Usage: ensure_message_dir <branch>
# Returns: 0 on success
ensure_message_dir() {
    branch="$1"
    msg_dir="$(get_message_dir "$branch")"
    mkdir -p "$msg_dir/queue" "$msg_dir/archive" 2>/dev/null || return 1
    return 0
}

# =============================================================================
# Send Message
# =============================================================================

# Send a message to a session's queue
# Usage: send_message <target_branch> <message> [sender_branch]
# Returns: 0 on success, 1 on failure
send_message() {
    target="$1"
    message="$2"
    sender="${3:-}"

    if [ -z "$target" ] || [ -z "$message" ]; then
        echo "Error: Target branch and message are required" >&2
        return 1
    fi

    # Determine sender (use current session's branch if not specified)
    if [ -z "$sender" ]; then
        sender="$(get_current_branch 2>/dev/null || echo 'unknown')"
    fi

    # Ensure message directory exists
    ensure_message_dir "$target" || return 1

    msg_dir="$(get_message_dir "$target")"

    # Generate unique filename: timestamp_sender_hash
    timestamp="$(date +%s)"
    hash="$(printf '%s%s%s' "$timestamp" "$sender" "$$" | cksum | cut -d' ' -f1)"
    # Sanitize sender for filename
    safe_sender="$(printf '%s' "$sender" | sed 's/[^a-zA-Z0-9_-]/_/g')"
    filename="${timestamp}_${safe_sender}_${hash}"
    msg_file="$msg_dir/queue/$filename"

    # Use atomic write via lock
    if try_lock "msg_$target"; then
        printf '%s\n' "$message" > "$msg_file"
        release_lock "msg_$target"
        return 0
    else
        # Fallback: write anyway (best effort)
        printf '%s\n' "$message" > "$msg_file"
        return 0
    fi
}

# =============================================================================
# Receive Messages
# =============================================================================

# Receive all pending messages for a branch
# Usage: recv_messages <branch> [--peek] [--archive]
# Options:
#   --peek    Don't remove messages after reading
#   --archive Move to archive instead of delete
# Returns: Messages on stdout (format: "FROM sender: message"), 0 if any, 1 if none
recv_messages() {
    branch=""
    peek=0
    archive=0

    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            --peek) peek=1; shift ;;
            --archive) archive=1; shift ;;
            -*) echo "Error: Unknown option '$1'" >&2; return 1 ;;
            *)
                if [ -z "$branch" ]; then
                    branch="$1"
                fi
                shift
                ;;
        esac
    done

    if [ -z "$branch" ]; then
        echo "Error: Branch name required" >&2
        return 1
    fi

    msg_dir="$(get_message_dir "$branch")"
    queue_dir="$msg_dir/queue"

    if [ ! -d "$queue_dir" ]; then
        return 1  # No messages
    fi

    # Check for messages - list files sorted by name (timestamp-based)
    msg_count=0

    # Get sorted list of message files
    for msg_file in "$queue_dir"/*; do
        [ -f "$msg_file" ] || continue

        msg_count=$((msg_count + 1))

        # Parse filename: timestamp_sender_hash
        filename="$(basename "$msg_file")"
        sender="$(echo "$filename" | cut -d'_' -f2)"

        # Read and output message
        message="$(cat "$msg_file")"
        printf 'FROM %s: %s\n' "$sender" "$message"

        # Handle message cleanup
        if [ "$peek" -eq 0 ]; then
            if [ "$archive" -eq 1 ]; then
                mv "$msg_file" "$msg_dir/archive/" 2>/dev/null || rm -f "$msg_file"
            else
                rm -f "$msg_file"
            fi
        fi
    done

    if [ "$msg_count" -eq 0 ]; then
        return 1
    fi

    return 0
}

# Count pending messages for a branch
# Usage: count_messages <branch>
# Returns: Count on stdout
count_messages() {
    branch="${1:-}"

    if [ -z "$branch" ]; then
        echo "0"
        return
    fi

    msg_dir="$(get_message_dir "$branch")"
    queue_dir="$msg_dir/queue"

    if [ ! -d "$queue_dir" ]; then
        echo "0"
        return
    fi

    # Count files in queue directory
    count=0
    for f in "$queue_dir"/*; do
        [ -f "$f" ] && count=$((count + 1))
    done

    echo "$count"
}

# =============================================================================
# Helper Functions
# =============================================================================

# Get current branch from tmux session mapping
# Usage: get_current_branch
# Returns: Branch name on stdout
get_current_branch() {
    current_session="$(get_current_session 2>/dev/null || true)"
    if [ -z "$current_session" ]; then
        return 1
    fi
    get_branch_for_session "$current_session"
}

# Clean up old messages (archive older than N days)
# Usage: cleanup_old_messages [days]
# Returns: 0 always
cleanup_old_messages() {
    days="${1:-7}"
    msg_base="${HYDRA_HOME:-$HOME/.hydra}/messages"

    if [ ! -d "$msg_base" ]; then
        return 0
    fi

    # Find and remove archived messages older than N days
    find "$msg_base" -path "*/archive/*" -type f -mtime +"$days" -delete 2>/dev/null || true

    # Remove empty directories
    find "$msg_base" -type d -empty -delete 2>/dev/null || true

    return 0
}

# Clean up messages for a dead session
# Usage: cleanup_messages_for_branch <branch>
# Returns: 0 always
cleanup_messages_for_branch() {
    branch="$1"
    if [ -z "$branch" ]; then
        return 0
    fi

    msg_dir="$(get_message_dir "$branch")"
    if [ -d "$msg_dir" ]; then
        rm -rf "$msg_dir" 2>/dev/null || true
    fi

    return 0
}
