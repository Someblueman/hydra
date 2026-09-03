#!/bin/sh
# Message queue functions for Hydra
# POSIX-compliant shell script
#
# Provides inter-session messaging via file-based queues.
# State v2 messages are stored under the project/head identity.

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
    _gmd_project="$(hydra_get_project_id 2>/dev/null)" || return 1
    _gmd_head="$(state_v2_find_head_by_branch "$_gmd_project" "$branch" 2>/dev/null)" || return 1
    _gmd_head_dir="$(state_v2_head_dir "$_gmd_project" "$_gmd_head")" || return 1
    printf '%s/messages\n' "$_gmd_head_dir"
}

get_message_lock() {
    _gml_branch="$1"
    _gml_project="$(hydra_get_project_id 2>/dev/null)" || return 1
    _gml_head="$(state_v2_find_head_by_branch "$_gml_project" "$_gml_branch" 2>/dev/null)" || return 1
    printf 'messages_%s_%s\n' "$_gml_project" "$_gml_head"
}

# Ensure message directories exist for a branch
# Usage: ensure_message_dir <branch>
# Returns: 0 on success
ensure_message_dir() {
    branch="$1"
    msg_dir="$(get_message_dir "$branch")" || return 1
    mkdir -p "$msg_dir/queue" "$msg_dir/archive" "$msg_dir/metadata" "$msg_dir/receipts" 2>/dev/null || return 1
    chmod 700 "$msg_dir" "$msg_dir/queue" "$msg_dir/archive" "$msg_dir/metadata" "$msg_dir/receipts" 2>/dev/null || true
    return 0
}

# =============================================================================
# Send Message
# =============================================================================

# Send a message to a session's queue
# Usage: send_message <target_branch> <message> [sender_branch] [type] [delivery]
# Returns: 0 on success, 1 on failure
send_message() {
    target="$1"
    message="$2"
    sender="${3:-}"
    msg_type="${4:-note}"
    delivery="${5:-inbox}"

    case "$msg_type" in note|request|steer|handoff|cancel) ;; *) echo "Error: unsupported message type '$msg_type'" >&2; return 1 ;; esac
    case "$delivery" in inbox|safe-point) ;; *) echo "Error: delivery must be inbox or safe-point" >&2; return 1 ;; esac

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

    msg_dir="$(get_message_dir "$target")" || return 1
    msg_lock="$(get_message_lock "$target")" || return 1

    # Generate unique filename: timestamp_sender_hash
    timestamp="$(date +%s)"
    hash="$(printf '%s%s%s' "$timestamp" "$sender" "$$" | cksum | cut -d' ' -f1)"
    # Sanitize sender for filename
    safe_sender="$(printf '%s' "$sender" | sed 's/[^a-zA-Z0-9_-]/_/g')"
    filename="${timestamp}_${safe_sender}_${hash}"
    msg_file="$msg_dir/queue/$filename"
    meta_file="$msg_dir/metadata/$filename"
    receipt_file="$msg_dir/receipts/$filename"
    target_instance=""
    if command -v hydra_get_project_id >/dev/null 2>&1 && command -v state_v2_find_head_by_branch >/dev/null 2>&1; then
        _sm_project="$(hydra_get_project_id 2>/dev/null || true)"
        _sm_head="$(state_v2_find_head_by_branch "$_sm_project" "$target" 2>/dev/null || true)"
        if [ -n "$_sm_head" ]; then
            _sm_head_dir="$(state_v2_head_dir "$_sm_project" "$_sm_head")"
            target_instance="$(sed -n '1p' "$_sm_head_dir/current-instance" 2>/dev/null || true)"
        fi
    fi

    # Use atomic write via lock with retries
    _retries=0
    while [ "$_retries" -lt 5 ]; do
        if try_lock "$msg_lock" "message append"; then
            while [ -e "$msg_file" ] || [ -e "$meta_file" ] || [ -e "$receipt_file" ]; do
                hash=$((hash + 1))
                filename="${timestamp}_${safe_sender}_${hash}"
                msg_file="$msg_dir/queue/$filename"
                meta_file="$msg_dir/metadata/$filename"
                receipt_file="$msg_dir/receipts/$filename"
            done
            _sm_body_tmp="$(mktemp_adjacent "$msg_file")" || { release_lock "$msg_lock"; return 1; }
            _sm_meta_tmp="$(mktemp_adjacent "$meta_file")" || { rm -f "$_sm_body_tmp"; release_lock "$msg_lock"; return 1; }
            _sm_receipt_tmp="$(mktemp_adjacent "$receipt_file")" || { rm -f "$_sm_body_tmp" "$_sm_meta_tmp"; release_lock "$msg_lock"; return 1; }
            printf '%s\n' "$message" > "$_sm_body_tmp"
            {
                printf 'type=%s\n' "$msg_type"
                printf 'delivery=%s\n' "$delivery"
                printf 'sender=%s\n' "$sender"
                printf 'target=%s\n' "$target"
                printf 'target_instance=%s\n' "$target_instance"
                printf 'queued_at=%s\n' "$timestamp"
            } > "$_sm_meta_tmp"
            {
                printf 'status=queued\n'
                printf 'updated_at=%s\n' "$timestamp"
                printf 'target_instance=%s\n' "$target_instance"
            } > "$_sm_receipt_tmp"
            chmod 600 "$_sm_body_tmp" "$_sm_meta_tmp" "$_sm_receipt_tmp" 2>/dev/null || true
            if ! atomic_replace "$meta_file" "$_sm_meta_tmp" || \
               ! atomic_replace "$receipt_file" "$_sm_receipt_tmp" || \
               ! atomic_replace "$msg_file" "$_sm_body_tmp"; then
                rm -f "$_sm_body_tmp" "$_sm_meta_tmp" "$_sm_receipt_tmp"
                release_lock "$msg_lock"
                return 1
            fi
            release_lock "$msg_lock"
            printf '%s\n' "$filename"
            return 0
        fi
        _retries=$((_retries + 1))
        sleep 0.05
    done

    echo "Error: Failed to acquire message lock for '$target'" >&2
    return 1
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
_message_write_receipt_locked() {
    _mwr_dir="$1"
    _mwr_id="$2"
    _mwr_status="$3"
    _mwr_instance="$4"
    case "$_mwr_status" in queued|delivered|stale) ;; *) return 1 ;; esac
    _mwr_path="$_mwr_dir/receipts/$_mwr_id"
    _mwr_tmp="$(mktemp_adjacent "$_mwr_path")" || return 1
    printf 'status=%s\nupdated_at=%s\ntarget_instance=%s\n' \
        "$_mwr_status" "$(date +%s)" "$_mwr_instance" > "$_mwr_tmp"
    chmod 600 "$_mwr_tmp" 2>/dev/null || true
    if ! atomic_replace "$_mwr_path" "$_mwr_tmp"; then
        rm -f "$_mwr_tmp"
        return 1
    fi
}

message_write_receipt() {
    _mwr_branch="$1"
    _mwr_id="$2"
    _mwr_status="$3"
    _mwr_instance="$4"
    _mwr_dir="$(get_message_dir "$_mwr_branch")"
    _mwr_lock="$(get_message_lock "$_mwr_branch")"
    acquire_lock "$_mwr_lock" "message receipt update" || return 1
    if ! _message_write_receipt_locked "$_mwr_dir" "$_mwr_id" "$_mwr_status" "$_mwr_instance"; then
        release_lock "$_mwr_lock"
        return 1
    fi
    release_lock "$_mwr_lock"
}

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

    msg_dir="$(get_message_dir "$branch")" || return 1
    queue_dir="$msg_dir/queue"

    if [ ! -d "$queue_dir" ]; then
        return 1  # No messages
    fi

    # Check for messages - list files sorted by name (timestamp-based)
    msg_count=0

    # Get sorted list of message files
    for msg_file in "$queue_dir"/*; do
        [ -f "$msg_file" ] || continue

        msg_lock="$(get_message_lock "$branch")"
        acquire_lock "$msg_lock" "message receive" || return 1
        [ -f "$msg_file" ] || { release_lock "$msg_lock"; continue; }

        msg_count=$((msg_count + 1))

        # Parse filename: timestamp_sender_hash
        filename="$(basename "$msg_file")"
        sender="${filename#*_}"
        sender="${sender%_*}"

        meta_file="$msg_dir/metadata/$filename"
        msg_type="$(sed -n 's/^type=//p' "$meta_file" 2>/dev/null || true)"
        delivery="$(sed -n 's/^delivery=//p' "$meta_file" 2>/dev/null || true)"
        target_instance="$(sed -n 's/^target_instance=//p' "$meta_file" 2>/dev/null || true)"
        msg_type="${msg_type:-note}"
        delivery="${delivery:-inbox}"
        current_instance=""
        if command -v hydra_get_project_id >/dev/null 2>&1 && command -v state_v2_find_head_by_branch >/dev/null 2>&1; then
            _rm_project="$(hydra_get_project_id 2>/dev/null || true)"
            _rm_head="$(state_v2_find_head_by_branch "$_rm_project" "$branch" 2>/dev/null || true)"
            if [ -n "$_rm_head" ]; then
                _rm_head_dir="$(state_v2_head_dir "$_rm_project" "$_rm_head")"
                current_instance="$(sed -n '1p' "$_rm_head_dir/current-instance" 2>/dev/null || true)"
            fi
        fi
        if [ -n "$target_instance" ] && [ "$target_instance" != "$current_instance" ]; then
            _message_write_receipt_locked "$msg_dir" "$filename" stale "$target_instance" || { release_lock "$msg_lock"; return 1; }
            mv "$msg_file" "$msg_dir/archive/$filename" 2>/dev/null || rm -f "$msg_file"
            release_lock "$msg_lock"
            continue
        fi

        message="$(cat "$msg_file")"
        if [ -f "$meta_file" ]; then
            printf 'FROM %s [%s/%s]: %s\n' "$sender" "$msg_type" "$delivery" "$message"
        else
            printf 'FROM %s: %s\n' "$sender" "$message"
        fi

        # Handle message cleanup
        if [ "$peek" -eq 0 ]; then
            if [ "$archive" -eq 1 ]; then
                mv "$msg_file" "$msg_dir/archive/" 2>/dev/null || rm -f "$msg_file"
            else
                rm -f "$msg_file"
            fi
            if [ -f "$meta_file" ]; then
                _message_write_receipt_locked "$msg_dir" "$filename" delivered "$current_instance" || { release_lock "$msg_lock"; return 1; }
            fi
        fi
        release_lock "$msg_lock"
    done

    if [ "$msg_count" -eq 0 ]; then
        return 1
    fi

    return 0
}

message_receipts() {
    _mr_branch="$1"
    _mr_json="${2:-}"
    _mr_dir="$(get_message_dir "$_mr_branch")"
    if [ "$_mr_json" = --json ]; then
        printf '{"schema_version":1,"ok":true,"command":"receipts","data":{"branch":"%s","receipts":[' "$(json_escape "$_mr_branch")"
    fi
    _mr_first=1
    for _mr_file in "$_mr_dir"/receipts/*; do
        [ -f "$_mr_file" ] || continue
        _mr_id="$(basename "$_mr_file")"
        _mr_status="$(sed -n 's/^status=//p' "$_mr_file")"
        _mr_at="$(sed -n 's/^updated_at=//p' "$_mr_file")"
        _mr_instance="$(sed -n 's/^target_instance=//p' "$_mr_file")"
        if [ "$_mr_json" = --json ]; then
            [ "$_mr_first" -eq 1 ] || printf ','
            _mr_first=0
            printf '{"message_id":"%s","status":"%s","updated_at":%s,"target_instance":%s}' \
                "$(json_escape "$_mr_id")" "$(json_escape "$_mr_status")" "${_mr_at:-0}" "$(json_string_or_null "$_mr_instance")"
        else
            printf '%s %s instance=%s updated=%s\n' "$_mr_id" "$_mr_status" "${_mr_instance:-unknown}" "${_mr_at:-unknown}"
        fi
    done
    [ "$_mr_json" != --json ] || printf ']}}\n'
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

    msg_dir="$(get_message_dir "$branch")" || return 1
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
    msg_base="${HYDRA_HOME:-$HOME/.hydra}"

    if [ ! -d "$msg_base" ]; then
        return 0
    fi

    # Find and remove archived messages older than N days
    find "$msg_base" -path "*/messages/archive/*" -type f -mtime +"$days" -delete 2>/dev/null || true

    # Remove empty directories
    find "$msg_base" -path "*/messages/*" -type d -empty -delete 2>/dev/null || true

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

    msg_dir="$(get_message_dir "$branch")" || return 1
    if [ -d "$msg_dir" ]; then
        case "$msg_dir" in
            "$HYDRA_HOME"/state/v2/projects/*/heads/*/messages)
                _cmb_lock="$(get_message_lock "$branch")"
                acquire_lock "$_cmb_lock" "archive messages on teardown" || return 1
                for _cmb_file in "$msg_dir"/queue/*; do
                    [ -f "$_cmb_file" ] || continue
                    mv "$_cmb_file" "$msg_dir/archive/" 2>/dev/null || true
                done
                release_lock "$_cmb_lock"
                ;;
            *) rm -rf "$msg_dir" 2>/dev/null || true ;;
        esac
    fi

    return 0
}
